#pragma once
#if defined(NBIO_ENABLE_RDMA) && defined(__linux__)

#include <nbio/async/coroutine.hpp>
#include <nbio/async/scheduler.hpp>
#include <nbio/async/task.hpp>
#include <nbio/net/rdma_connector.hpp>
#include <nbio/net/rdma_result.hpp>
#include <nbio/net/rdma_resource_manager.hpp>
#include <nbio/net/address.hpp>
#include <nbio/net/payload.hpp>
#include <nbio/async/runtime.hpp>
#include <system_error>
#include <utility>

#include <nbio/core/channel.hpp>

namespace nbio::net {
class RdmaConnectChannel final : public nbio::core::Channel<RdmaConnectChannel> {
   public:
    using Handle = int;
    using Payload = detail::PollPayload<RdmaConnectChannel>;

    struct PendingConnect {
        std::error_code error{};
    };

    // The connection to establish: connect() finishes it and hands back a session
    // that owns it.
    RdmaConnectChannel(nbio::net::RdmaConnector& connector, nbio::core::Multiplexer& multiplexer,
                       nbio::async::Scheduler& scheduler);
    ~RdmaConnectChannel() noexcept;

    nbio::async::Task<RdmaResult<void>> Connect(nbio::net::Address peer);

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
