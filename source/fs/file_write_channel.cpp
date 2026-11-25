#include <nbio/async/coroutine.hpp>
#include <nbio/fs/file.hpp>
#include <nbio/fs/file_stream.hpp>
#include <nbio/fs/file_write_channel.hpp>
#include <optional>
#include <span>
#include <utility>

namespace nbio::fs {
class WriteAwaiter {
public:
    WriteAwaiter(FileWriteChannel& channel, std::span<const char> buffer, std::uint64_t offset)
        : channel_(channel), buffer_(buffer), offset_(offset) {}

    bool await_ready() const noexcept { return false; }

    template <typename PromiseType>
    bool await_suspend(std::coroutine_handle<PromiseType> handle) {
        transmission_.status = OperationStatus::kPending;
        transmission_.bytes = 0;
        transmission_.offset = offset_;
        transmission_.error_code = {};
        // The transmission buffer is non-const only for C API compatibility: the
        // backend reads it, never writes through it.
        transmission_.buffer = std::span<char>(const_cast<char*>(buffer_.data()), buffer_.size());
        channel_.Arm();
        try {
            channel_.prepare(async::Coroutine::FromHandle(handle), &transmission_);
        } catch (...) {
            if (channel_.is_idle()) channel_.Disarm();
            throw;
        }
        return true;
    }

    fs::Transmission await_resume() const noexcept { return transmission_; }

private:
    FileWriteChannel& channel_;
    std::span<const char> buffer_;
    std::uint64_t offset_;
    fs::Transmission transmission_{};
};

FileWriteChannel::FileWriteChannel(FileStream& file_stream, nbio::core::Multiplexer& multiplexer,
                                   nbio::async::Scheduler& scheduler)
    : nbio::core::Channel<FileWriteChannel>(nbio::core::ChannelType::kWrite, file_stream.native_handle(), multiplexer,
                                            scheduler),
      file_stream_(file_stream) {
    // Registered on the first arm(): a file is always ready, so the channel is
    // only watched while a write is queued.
}

FileWriteChannel::~FileWriteChannel() noexcept { multiplexer_.DeleteChannel(this); }

void FileWriteChannel::prepare(async::Coroutine waiter, fs::Transmission* transmission) {
    auto owner = waiter.Lock();
    waiters_.push_back(Pending{std::move(waiter), owner});
    bool submitted = false;
    try {
        payload_.Submit(transmission);
        submitted = true;
        file_stream_.Enqueue(FileStream::Direction::kWrite);
    } catch (...) {
        if (submitted) payload_.UndoSubmit(transmission);
        waiters_.pop_back();
        throw;
    }
    owner->PinIO();
    if (!transmission->buffer.empty()) file_stream_.ObserveWrite(transmission->offset + transmission->buffer.size());
}

FileWriteChannel::Payload& FileWriteChannel::Submit() {
    constexpr auto direction = FileStream::Direction::kWrite;
    if (!file_stream_.CanSubmit(direction)) return inactive_payload_;
    payload_.SetSubmissionLimit(file_stream_.SubmissionLimit(direction));
    return payload_;
}

void FileWriteChannel::CompleteOne() {
    auto pending = std::move(waiters_.front());
    waiters_.pop_front();
    payload_.Conclude();
    pending.owner->UnpinIO();
    scheduler_.Submit(std::move(pending.waiter));
}

void FileWriteChannel::Complete() { file_stream_.Complete(FileStream::Direction::kWrite); }

nbio::async::Task<utility::expected<std::size_t, std::error_code>> FileWriteChannel::Write(
    std::span<const char> buffer, std::uint64_t offset) {
    if (file_stream_.error_) co_return utility::unexpected<std::error_code>(file_stream_.error_);
    if (!detail::ValidFileRange(offset, buffer.size())) {
        co_return utility::unexpected<std::error_code>(std::make_error_code(std::errc::invalid_argument));
    }
    auto result = co_await WriteAwaiter{*this, buffer, offset};
    if (result.status == OperationStatus::kError) {
        co_return utility::unexpected<std::error_code>(std::move(result.error_code));
    }
    co_return result.bytes;
}
}  // namespace nbio::fs
