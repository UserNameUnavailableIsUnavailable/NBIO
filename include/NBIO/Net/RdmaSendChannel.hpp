#pragma once
#if defined(NBIO_ENABLE_RDMA) && defined(__linux__)

#include <NBIO/Async/Coroutine.hpp>
#include <NBIO/Async/Scheduler.hpp>
#include <NBIO/Async/Task.hpp>
#include <NBIO/Net/RdmaConnector.hpp>
#include <NBIO/Net/RdmaResult.hpp>
#include <NBIO/Net/Payload.hpp>
#include <NBIO/Async/Runtime.hpp>
#include <span>
#include <system_error>
#include <utility>

#include <NBIO/Core/Channel.hpp>

namespace NBIO::Net {
class RdmaSendChannel final : public NBIO::Core::Channel<RdmaSendChannel> {
   public:
    using Handle = int;
    using Payload = detail::PollPayload<RdmaSendChannel>;

    struct PendingSend {
        // How many sends may still be in flight for the Parked poll to be met.
        std::size_t target{0};
        // Completions reaped since the poll began.
        std::size_t completions{0};
        // Why the stream stopped completing anything, empty while it has not.
        std::error_code error{};
    };

    RdmaSendChannel(NBIO::Net::RdmaConnector& connection, NBIO::Core::Multiplexer& multiplexer,
                    NBIO::Async::Scheduler& scheduler);
    ~RdmaSendChannel() noexcept;

    // A chunk to fill. Several can be held at once, so the way to use this is to
    // take as many as the stream will give, fill them, and send them -- the
    // device carries them in parallel instead of one per round trip. An empty
    // answer means every chunk is already in flight.
    RdmaResult<RdmaBufferResult> Acquire() noexcept { return connection_.Acquire(); }

    // Hands one acquired chunk to the device. Returns immediately: the chunk
    // belongs to the device until a completion retires it, which poll() reports.
    RdmaResult<void> Send(std::span<char> chunk, std::size_t length) noexcept {
        return connection_.Send(chunk, length);
    }

    // Waits until at least `count` of the sends in flight when it was called have
    // completed, or -- for the default -- until all of them have, after which
    // every chunk has been handed back. Answers how many completed while it
    // waited, which is also how many chunks became available, or why the stream
    // gave up completing them.
    NBIO::Async::Task<RdmaResult<std::size_t>> Poll(std::size_t count = 0);

    // Sends posted and not yet reaped.
    std::size_t outstanding() const noexcept { return connection_.outstanding_sends(); }

    // The operation this channel wants from the backend is a one-shot poll; the
    // payload carries only whether one is already out there.
    Payload& Submit();
    void Complete();

    void Park(NBIO::Async::Coroutine waiter) noexcept { waiter_ = std::move(waiter); }

    NBIO::Net::RdmaConnector& connection() noexcept { return connection_; }

    PendingSend& job() noexcept { return job_; }

    const PendingSend& job() const noexcept { return job_; }

    // Completions reaped so far, which a poll reads to answer with a difference.
    std::size_t& completed() noexcept { return completed_; }

   private:
    NBIO::Net::RdmaConnector& connection_;
    PendingSend job_{};
    std::size_t completed_{0};
    NBIO::Async::Coroutine waiter_{};
    Payload payload_{};
};
}  // namespace NBIO::Net

#endif  // defined(NBIO_ENABLE_RDMA) && defined(__linux__)
