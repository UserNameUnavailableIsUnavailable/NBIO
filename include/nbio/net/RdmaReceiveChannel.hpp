#pragma once
#if defined(NBIO_ENABLE_RDMA) && defined(__linux__)

#include <nbio/async/Coroutine.hpp>
#include <nbio/async/Scheduler.hpp>
#include <nbio/async/Task.hpp>
#include <nbio/net/RdmaConnector.hpp>
#include <nbio/net/RdmaResult.hpp>
#include <nbio/net/Payload.hpp>
#include <nbio/runtime/Runtime.hpp>
#include <system_error>
#include <utility>

#include <nbio/core/Channel.hpp>

namespace nbio::net {
class RdmaReceiveChannel final : public nbio::core::Channel<RdmaReceiveChannel> {
   public:
    using Handle = int;
    using Payload = detail::PollPayload<RdmaReceiveChannel>;

    struct PendingReceive {
        RdmaReceiveResult result{};
        std::error_code error{};
    };

    ~RdmaReceiveChannel() noexcept;

    RdmaReceiveChannel(nbio::net::RdmaConnector& connection, nbio::core::Multiplexer& multiplexer,
                       nbio::async::Scheduler& scheduler);

    nbio::async::Task<nbio::Runtime, RdmaResult<RdmaReceiveResult>> Receive();

    // The same, for a caller that is willing to carry on without one.
    nbio::async::Task<nbio::Runtime, RdmaResult<RdmaReceiveResult>> TryReceive();

    // Hands a received chunk back for the next message.
    RdmaResult<void> Release(std::span<char> chunk) noexcept;

    // The operation this channel wants from the backend is a one-shot poll; the
    // payload carries only whether one is already out there.
    Payload& Submit();
    void Complete();

    void Park(nbio::async::Coroutine waiter) noexcept { waiter_ = std::move(waiter); }

    nbio::net::RdmaConnector& connection() noexcept { return connection_; }

    PendingReceive& job() noexcept { return job_; }

    const PendingReceive& job() const noexcept { return job_; }

   private:
    nbio::net::RdmaConnector& connection_;
    PendingReceive job_{};
    nbio::async::Coroutine waiter_{};
    Payload payload_{};
};
}  // namespace nbio::net

#endif  // defined(NBIO_ENABLE_RDMA) && defined(__linux__)
