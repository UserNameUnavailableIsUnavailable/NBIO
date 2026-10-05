#pragma once
#if defined(NBIO_ENABLE_RDMA) && defined(__linux__)

#include <nbio/async/Coroutine.hpp>
#include <nbio/async/Scheduler.hpp>
#include <nbio/async/Task.hpp>
#include <nbio/net/RdmaAcceptor.hpp>
#include <nbio/net/RdmaResult.hpp>
#include <nbio/net/Payload.hpp>
#include <nbio/async/Runtime.hpp>
#include <memory>
#include <optional>
#include <system_error>
#include <utility>

#include <nbio/core/Channel.hpp>
#include "RdmaSessionService.hpp"

namespace nbio::net {
class RdmaAcceptChannel final : public nbio::Core::Channel<RdmaAcceptChannel> {
   public:
    using Handle = int;
    using Payload = detail::PollPayload<RdmaAcceptChannel>;

    struct PendingAccept {
        std::optional<RdmaConnector> connection{};
        std::error_code error{};
    };

    RdmaAcceptChannel(nbio::net::RdmaAcceptor& acceptor, nbio::Core::Multiplexer& multiplexer,
                      nbio::async::Scheduler& scheduler);
    ~RdmaAcceptChannel() noexcept;

    nbio::async::Task<RdmaResult<RdmaConnector>> accept();

    // The operation this channel wants from the backend is a one-shot poll; the
    // payload carries only whether one is already out there.
    Payload& Submit();
    void Complete();

    void Park(nbio::async::Coroutine waiter) noexcept { waiter_ = std::move(waiter); }

    PendingAccept& job() noexcept { return job_; }

    const PendingAccept& job() const noexcept { return job_; }

    nbio::net::RdmaAcceptor& acceptor() noexcept { return acceptor_; }

   private:
    nbio::net::RdmaAcceptor& acceptor_;
    PendingAccept job_{};
    nbio::async::Coroutine waiter_{};
    Payload payload_{};
};
}  // namespace nbio::net

#endif  // defined(NBIO_ENABLE_RDMA) && defined(__linux__)
