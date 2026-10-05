#pragma once

#include <NBIO/Async/Scheduler.hpp>
#include <NBIO/Async/Task.hpp>
#include <NBIO/Utility/Buffer.hpp>
#include <NBIO/FS/File.hpp>
#include <NBIO/Core/Channel.hpp>
#include <NBIO/Core/Types.hpp>
#include <NBIO/FS/Payload.hpp>
#include <NBIO/Async/Runtime.hpp>
#include <deque>
#include <optional>
#include <span>
#include <system_error>

namespace NBIO::FS {
class FileStream;

class ReadAwaiter;

class FileReadChannel final : public NBIO::Core::Channel<FileReadChannel> {
   public:
    using Payload = detail::IOVectorPayload<FileReadChannel>;

    FileReadChannel(FileStream& file, NBIO::Core::Multiplexer& multiplexer,
                    NBIO::Async::Scheduler& scheduler);
    ~FileReadChannel() noexcept;

    NBIO::Async::Task<Utility::expected<std::size_t, std::error_code>> read(std::span<char> buffer);

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
    void Prepare(Async::Coroutine waiter, FS::Transmission* transmission);

    FileStream& file_stream_;
    // One waiter per transmission, in queue order.
    std::deque<Async::Coroutine> waiters_;
    Payload payload_{};
};
}  // namespace NBIO::FS




