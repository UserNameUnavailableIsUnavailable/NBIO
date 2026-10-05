#pragma once

#include <nbio/async/Coroutine.hpp>
#include <nbio/async/Scheduler.hpp>
#include <nbio/async/Task.hpp>
#include <nbio/utility/Buffer.hpp>
#include <nbio/fs/File.hpp>
#include <nbio/core/Channel.hpp>
#include <nbio/core/Types.hpp>
#include <nbio/fs/Payload.hpp>
#include <nbio/async/Runtime.hpp>
#include <deque>
#include <optional>
#include <span>
#include <system_error>

namespace nbio::fs {
class FileStream;

class WriteAwaiter;

class FileWriteChannel final : public nbio::Core::Channel<FileWriteChannel> {
   public:
    using Payload = detail::IOVectorPayload<FileWriteChannel>;

    FileWriteChannel(FileStream& file, nbio::Core::Multiplexer& multiplexer,
                     nbio::async::Scheduler& scheduler);
    ~FileWriteChannel() noexcept;

    nbio::async::Task<utility::expected<std::size_t, std::error_code>> write(std::span<const char> buffer);

    // The operation the backend performs lives in the payload; the backend fills
    // the transmissions and asks the channel to reap them.
    Payload& Submit();
    void Complete();

    FileStream& file_stream() noexcept { return file_stream_; }
    const FileStream& file_stream() const noexcept { return file_stream_; }

   private:
    friend class WriteAwaiter;

    // Queues the write and arms the channel: this is the suspension point, and
    // being armed is what tells the backend to look at the channel.
    void prepare(async::Coroutine waiter, fs::Transmission* transmission);

    FileStream& file_stream_;
    // One waiter per transmission, in queue order.
    std::deque<async::Coroutine> waiters_;
    Payload payload_{};
};
}  // namespace nbio::fs




