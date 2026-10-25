#pragma once
#if defined(NBIO_ENABLE_RDMA) && defined(__linux__)

#include <nbio/async/coroutine.hpp>
#include <nbio/runtime/daemon.hpp>
#include <nbio/async/scheduler.hpp>
#include <nbio/async/task.hpp>
#include <nbio/core/channel.hpp>
#include <nbio/net/address.hpp>
#include <nbio/net/payload.hpp>
#include <nbio/net/rdma_connector.hpp>
#include <nbio/net/rdma_resource.hpp>
#include <nbio/net/rdma_result.hpp>
#include <system_error>
#include <utility>

namespace nbio::net {
class RdmaConnectChannel final : public nbio::core::Channel<RdmaConnectChannel> {
   public:
    using Handle = int;
    using Payload = detail::PollPayload<RdmaConnectChannel>;

    struct PendingConnect {
        std::error_code error{};
    };

    // The connection to establish: target() prepares it, establish() submits it, and
    // the session that owns it is built once it is up.
    RdmaConnectChannel(nbio::net::RdmaConnector& connector, nbio::core::Multiplexer& multiplexer,
                       nbio::async::Scheduler& scheduler);
    ~RdmaConnectChannel() noexcept;

    /** @brief Pins the local address this connection comes from. */
    RdmaResult<void> BindSource(const nbio::net::Address& local) noexcept;

    /**
     * @brief Resolves the destination and builds the queue pair, without connecting.
     *
     * @details When this returns, receives can be posted; the peer cannot send until
     * @ref Establish.
     */
    RdmaResult<void> BindTarget(const nbio::net::Address& peer) noexcept;

    /** @brief Submits the connection and parks until it is established. */
    nbio::async::Task<RdmaResult<void>> Establish();

    /**
     * @brief Takes at most one non-blocking step of the handshake.
     *
     * @details Answers @ref RdmaErrc::kNotReady while the connection is not up yet.
     * @ref Establish parks on the event fd and calls this once per wake, which is
     * how the channel integrates with the multiplexer without blocking a thread.
     */
    RdmaResult<void> Advance() noexcept;

    // The operation this channel wants from the backend is a one-shot poll; the
    // payload carries only whether one is already out there.
    Payload& Submit();
    void Complete();

    void Park(nbio::async::Coroutine waiter) noexcept { waiter_ = std::move(waiter); }

    PendingConnect& job() noexcept { return job_; }

    const PendingConnect& job() const noexcept { return job_; }

    nbio::net::RdmaConnector& connector() noexcept { return connector_; }

   private:
    nbio::net::RdmaConnector& connector_;
    nbio::net::Address peer_{};
    PendingConnect job_{};
    nbio::async::Coroutine waiter_{};
    Payload payload_{};
};
}  // namespace nbio::net

#endif  // defined(NBIO_ENABLE_RDMA) && defined(__linux__)
