#pragma once
#if defined(NBIO_ENABLE_RDMA) && defined(__linux__)

#include <memory>
#include <nbio/async/coroutine.hpp>
#include <nbio/runtime/daemon.hpp>
#include <nbio/async/scheduler.hpp>
#include <nbio/async/task.hpp>
#include <nbio/core/channel.hpp>
#include <nbio/net/payload.hpp>
#include <nbio/net/rdma_listener.hpp>
#include <nbio/net/rdma_result.hpp>
#include <optional>
#include <system_error>
#include <utility>

#include "rdma_session_service.hpp"

namespace nbio::net {
class RdmaListenChannel final : public nbio::core::Channel<RdmaListenChannel> {
   public:
    using Handle = int;
    using Payload = detail::PollPayload<RdmaListenChannel>;

    struct PendingAccept {
        std::optional<RdmaConnector> connection{};
        std::error_code error{};
    };

    RdmaListenChannel(nbio::net::RdmaListener& listener, nbio::core::Multiplexer& multiplexer,
                      nbio::async::Scheduler& scheduler);
    ~RdmaListenChannel() noexcept;

    nbio::async::Task<RdmaResult<RdmaConnector>> accept();

    // The operation this channel wants from the backend is a one-shot poll; the
    // payload carries only whether one is already out there.
    Payload& Submit();
    void Complete();

    void Park(nbio::async::Coroutine waiter) noexcept { waiter_ = std::move(waiter); }

    PendingAccept& job() noexcept { return job_; }

    const PendingAccept& job() const noexcept { return job_; }

    nbio::net::RdmaListener& listener() noexcept { return listener_; }

   private:
    nbio::net::RdmaListener& listener_;
    PendingAccept job_{};
    nbio::async::Coroutine waiter_{};
    Payload payload_{};
};
}  // namespace nbio::net

#endif  // defined(NBIO_ENABLE_RDMA) && defined(__linux__)
