#pragma once
#if defined(NBIO_ENABLE_RDMA) && defined(__linux__)

#include <nbio/async/coroutine.hpp>
#include <nbio/async/scheduler.hpp>
#include <nbio/async/task.hpp>
#include <nbio/core/channel.hpp>
#include <nbio/net/payload.hpp>
#include <nbio/net/rdma_connector.hpp>
#include <optional>
#include <system_error>
#include <utility>

namespace nbio::net {
/**
 * @brief The asynchronous half of an outgoing connection: resolves the destination
 *        with the multiplexer driving the CM fd, and hands back the prepared
 *        communication.
 */
class RdmaConnectChannel final : public nbio::core::Channel<RdmaConnectChannel> {
   public:
    using Handle = int;
    using Payload = detail::PollPayload<RdmaConnectChannel>;

    struct PendingConnect {
        std::optional<RdmaCommunication> communication{};
        std::error_code error{};
    };

    RdmaConnectChannel(nbio::net::RdmaConnector& connector, nbio::core::Multiplexer& multiplexer,
                       nbio::async::Scheduler& scheduler);
    ~RdmaConnectChannel() noexcept;

    /** @brief Resolves the destination and hands back the prepared communication. */
    nbio::async::Task<RdmaResult<RdmaCommunication>> Connect(const net::Address& target);

    Payload& Submit();
    void Complete();
    void Park(nbio::async::Coroutine waiter) noexcept { waiter_ = std::move(waiter); }

    PendingConnect& job() noexcept { return job_; }
    const PendingConnect& job() const noexcept { return job_; }
    nbio::net::RdmaConnector& connector() noexcept { return connector_; }

    // One non-blocking step of the resolution; kNotReady until the communication is
    // ready. This is what the channel drives.
    RdmaResult<RdmaCommunication> Advance() noexcept;
    // Submits the address resolve; its completion arrives as ADDR_RESOLVED, which
    // the multiplexer wakes this channel for.
    RdmaResult<void> SubmitResolve(const net::Address& target) noexcept;

   private:
    nbio::net::RdmaConnector& connector_;
    PendingConnect job_{};
    nbio::async::Coroutine waiter_{};
    Payload payload_{};
};
}  // namespace nbio::net

#endif  // defined(NBIO_ENABLE_RDMA) && defined(__linux__)
