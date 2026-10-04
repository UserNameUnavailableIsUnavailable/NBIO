#pragma once
#if defined(NBIO_ENABLE_RDMA) && defined(__linux__)

#include <async/Coroutine.hpp>
#include <async/Scheduler.hpp>
#include <async/Task.hpp>
#include <net/RdmaAcceptor.hpp>
#include <net/Payload.hpp>
#include <runtime/Runtime.hpp>
#include <memory>
#include <string>
#include <utility>

#include <core/Channel.hpp>
#include "RdmaSessionService.hpp"

namespace nbio::net {
class RdmaAcceptChannel final : public nbio::core::Channel<RdmaAcceptChannel> {
   public:
    using Handle = int;
    using Payload = detail::PollPayload<RdmaAcceptChannel>;

    struct PendingAccept {
        std::shared_ptr<RdmaSessionService> session{};
        // Why no connection can be admitted any more, empty while none has
        // failed.
        std::string error{};
    };

    RdmaAcceptChannel(nbio::net::RdmaAcceptor& acceptor, nbio::core::Multiplexer& multiplexer,
                      nbio::async::Scheduler& scheduler);
    ~RdmaAcceptChannel() noexcept;

    nbio::async::Task<nbio::runtime, nbio::utility::expected<std::shared_ptr<RdmaSessionService>, std::string>> accept();

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
