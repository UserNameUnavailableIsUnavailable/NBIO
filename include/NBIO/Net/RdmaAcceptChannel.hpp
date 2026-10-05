#pragma once
#if defined(NBIO_ENABLE_RDMA) && defined(__linux__)

#include <NBIO/Async/Coroutine.hpp>
#include <NBIO/Async/Scheduler.hpp>
#include <NBIO/Async/Task.hpp>
#include <NBIO/Net/RdmaAcceptor.hpp>
#include <NBIO/Net/RdmaResult.hpp>
#include <NBIO/Net/Payload.hpp>
#include <NBIO/Async/Runtime.hpp>
#include <memory>
#include <optional>
#include <system_error>
#include <utility>

#include <NBIO/Core/Channel.hpp>
#include "RdmaSessionService.hpp"

namespace NBIO::Net {
class RdmaAcceptChannel final : public NBIO::Core::Channel<RdmaAcceptChannel> {
   public:
    using Handle = int;
    using Payload = detail::PollPayload<RdmaAcceptChannel>;

    struct PendingAccept {
        std::optional<RdmaConnector> connection{};
        std::error_code error{};
    };

    RdmaAcceptChannel(NBIO::Net::RdmaAcceptor& acceptor, NBIO::Core::Multiplexer& multiplexer,
                      NBIO::Async::Scheduler& scheduler);
    ~RdmaAcceptChannel() noexcept;

    NBIO::Async::Task<RdmaResult<RdmaConnector>> accept();

    // The operation this channel wants from the backend is a one-shot poll; the
    // payload carries only whether one is already out there.
    Payload& Submit();
    void Complete();

    void Park(NBIO::Async::Coroutine waiter) noexcept { waiter_ = std::move(waiter); }

    PendingAccept& job() noexcept { return job_; }

    const PendingAccept& job() const noexcept { return job_; }

    NBIO::Net::RdmaAcceptor& acceptor() noexcept { return acceptor_; }

   private:
    NBIO::Net::RdmaAcceptor& acceptor_;
    PendingAccept job_{};
    NBIO::Async::Coroutine waiter_{};
    Payload payload_{};
};
}  // namespace NBIO::Net

#endif  // defined(NBIO_ENABLE_RDMA) && defined(__linux__)
