#pragma once
#if defined(NBIO_ENABLE_RDMA) && defined(__linux__)

#include <nbio/async/coroutine.hpp>
#include <nbio/runtime/daemon.hpp>
#include <nbio/async/scheduler.hpp>
#include <nbio/async/task.hpp>
#include <nbio/core/channel.hpp>
#include <nbio/net/payload.hpp>
#include <nbio/net/rdma_connector.hpp>
#include <nbio/net/rdma_result.hpp>
#include <system_error>
#include <utility>

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

    nbio::async::Task<RdmaResult<RdmaReceiveResult>> Receive();

    // The same, for a caller that is willing to carry on without one.
    nbio::async::Task<RdmaResult<RdmaReceiveResult>> TryReceive();

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
