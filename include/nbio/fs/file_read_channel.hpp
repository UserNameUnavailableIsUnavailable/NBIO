#pragma once

#include <nbio/async/scheduler.hpp>
#include <nbio/async/task.hpp>
#include <nbio/utility/buffer.hpp>
#include <nbio/fs/file.hpp>
#include <nbio/core/channel.hpp>
#include <nbio/core/types.hpp>
#include <nbio/fs/payload.hpp>
#include <nbio/async/runtime.hpp>
#include <deque>
#include <optional>
#include <span>
#include <system_error>

namespace nbio::fs {
class FileStream;

class ReadAwaiter;

class FileReadChannel final : public nbio::core::Channel<FileReadChannel> {
   public:
    using Payload = detail::IOVectorPayload<FileReadChannel>;

    FileReadChannel(FileStream& file, nbio::core::Multiplexer& multiplexer,
                    nbio::async::Scheduler& scheduler);
    ~FileReadChannel() noexcept;

    nbio::async::Task<utility::expected<std::size_t, std::error_code>> read(std::span<char> buffer);

    // The operation the backend performs lives in the payload; the backend fills
    // the transmissions and asks the channel to reap them.
    Payload& Submit();
    void Complete();

    FileStream& file_stream() noexcept { return file_stream_; }
    const FileStream& file_stream() const noexcept { return file_stream_; }

   private:
    friend class ReadAwaiter;

    // Queues the read and arms the channel: this is the suspension point, and
    // being armed is what tells the backend to look at the channel.
    void Prepare(async::Coroutine waiter, fs::Transmission* transmission);

    FileStream& file_stream_;
    // One waiter per transmission, in queue order.
    std::deque<async::Coroutine> waiters_;
    Payload payload_{};
};
}  // namespace nbio::fs




