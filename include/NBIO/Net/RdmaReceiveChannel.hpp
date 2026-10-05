#pragma once
#if defined(NBIO_ENABLE_RDMA) && defined(__linux__)

#include <NBIO/Async/Coroutine.hpp>
#include <NBIO/Async/Scheduler.hpp>
#include <NBIO/Async/Task.hpp>
#include <NBIO/Net/RdmaConnector.hpp>
#include <NBIO/Net/RdmaResult.hpp>
#include <NBIO/Net/Payload.hpp>
#include <NBIO/Async/Runtime.hpp>
#include <system_error>
#include <utility>

#include <NBIO/Core/Channel.hpp>

namespace NBIO::Net {
class RdmaReceiveChannel final : public NBIO::Core::Channel<RdmaReceiveChannel> {
   public:
    using Handle = int;
    using Payload = detail::PollPayload<RdmaReceiveChannel>;

    struct PendingReceive {
        RdmaReceiveResult result{};
        std::error_code error{};
    };

    ~RdmaReceiveChannel() noexcept;

    RdmaReceiveChannel(NBIO::Net::RdmaConnector& connection, NBIO::Core::Multiplexer& multiplexer,
                       NBIO::Async::Scheduler& scheduler);

    NBIO::Async::Task<RdmaResult<RdmaReceiveResult>> Receive();

    // The same, for a caller that is willing to carry on without one.
    NBIO::Async::Task<RdmaResult<RdmaReceiveResult>> TryReceive();

    // Hands a received chunk back for the next message.
    RdmaResult<void> Release(std::span<char> chunk) noexcept;

    // The operation this channel wants from the backend is a one-shot poll; the
    // payload carries only whether one is already out there.
    Payload& Submit();
    void Complete();

    void Park(NBIO::Async::Coroutine waiter) noexcept { waiter_ = std::move(waiter); }

    NBIO::Net::RdmaConnector& connection() noexcept { return connection_; }

    PendingReceive& job() noexcept { return job_; }

    const PendingReceive& job() const noexcept { return job_; }

   private:
    NBIO::Net::RdmaConnector& connection_;
    PendingReceive job_{};
    NBIO::Async::Coroutine waiter_{};
    Payload payload_{};
};
}  // namespace NBIO::Net

#endif  // defined(NBIO_ENABLE_RDMA) && defined(__linux__)
