#include <nbio/fs/file.hpp>
#include <nbio/fs/file_read_channel.hpp>
#include <nbio/fs/file_stream.hpp>
#include <optional>
#include <span>
#include <utility>

namespace nbio::fs {
class ReadAwaiter {
public:
    ReadAwaiter(FileReadChannel& channel, std::span<char> buffer, std::uint64_t offset)
        : channel_(channel), buffer_(buffer), offset_(offset) {}

    bool await_ready() const noexcept { return false; }

    template <typename PromiseType>
    bool await_suspend(std::coroutine_handle<PromiseType> handle) {
        transmission_.status = OperationStatus::kPending;
        transmission_.bytes = 0;
        transmission_.offset = offset_;
        transmission_.error_code = {};
        transmission_.buffer = buffer_;
        channel_.Arm();
        try {
            channel_.Prepare(async::Coroutine::FromHandle(handle), &transmission_);
        } catch (...) {
            if (channel_.is_idle()) channel_.Disarm();
            throw;
        }
        return true;
    }

    fs::Transmission await_resume() const noexcept { return transmission_; }

private:
    FileReadChannel& channel_;
    std::span<char> buffer_;
    std::uint64_t offset_;
    fs::Transmission transmission_{};
};

FileReadChannel::FileReadChannel(FileStream& file, nbio::core::Multiplexer& multiplexer,
                                 nbio::async::Scheduler& scheduler)
    : nbio::core::Channel<FileReadChannel>(nbio::core::ChannelType::kRead,
                                           static_cast<std::uintptr_t>(file.native_handle()), multiplexer, scheduler),
      file_stream_(file) {
    // Registered on the first arm(): a file is always ready, so the channel is
    // only watched while a read is queued.
}

FileReadChannel::~FileReadChannel() noexcept { multiplexer_.DeleteChannel(this); }

void FileReadChannel::Prepare(async::Coroutine waiter, fs::Transmission* transmission) {
    auto owner = waiter.Lock();
    waiters_.push_back(Pending{std::move(waiter), owner});
    bool submitted = false;
    try {
        payload_.Submit(transmission);
        submitted = true;
        file_stream_.Enqueue(FileStream::Direction::kRead);
    } catch (...) {
        if (submitted) payload_.UndoSubmit(transmission);
        waiters_.pop_back();
        throw;
    }
    owner->PinIO();
}

FileReadChannel::Payload& FileReadChannel::Submit() {
    constexpr auto direction = FileStream::Direction::kRead;
    if (!file_stream_.CanSubmit(direction)) return inactive_payload_;
    payload_.SetSubmissionLimit(file_stream_.SubmissionLimit(direction));
    return payload_;
}

void FileReadChannel::CompleteOne() {
    auto pending = std::move(waiters_.front());
    waiters_.pop_front();
    payload_.Conclude();
    pending.owner->UnpinIO();
    scheduler_.Submit(std::move(pending.waiter));
}

void FileReadChannel::Complete() { file_stream_.Complete(FileStream::Direction::kRead); }

nbio::async::Task<utility::expected<std::size_t, std::error_code>> FileReadChannel::Read(std::span<char> buffer,
                                                                                       std::uint64_t offset) {
    if (file_stream_.error_) co_return utility::unexpected<std::error_code>(file_stream_.error_);
    if (!detail::ValidFileRange(offset, buffer.size())) {
        co_return utility::unexpected<std::error_code>(std::make_error_code(std::errc::invalid_argument));
    }
    auto result = co_await ReadAwaiter{*this, buffer, offset};
    if (result.status == OperationStatus::kError) {
        co_return utility::unexpected<std::error_code>(std::move(result.error_code));
    }
    co_return result.bytes;
}
}  // namespace nbio::fs
