#pragma once
#if defined(NBIO_ENABLE_RDMA) && defined(__linux__)

#include <NBIO/Async/Coroutine.hpp>
#include <NBIO/Async/Scheduler.hpp>
#include <NBIO/Async/Task.hpp>
#include <NBIO/Net/RdmaConnector.hpp>
#include <NBIO/Net/RdmaResult.hpp>
#include <NBIO/Net/RdmaResourceManager.hpp>
#include <NBIO/Net/Address.hpp>
#include <NBIO/Net/Payload.hpp>
#include <NBIO/Async/Runtime.hpp>
#include <memory>
#include <system_error>
#include <utility>

#include <NBIO/Core/Channel.hpp>

namespace NBIO::Net {
class RdmaConnectChannel final : public NBIO::Core::Channel<RdmaConnectChannel> {
   public:
    using Handle = int;
    using Payload = detail::PollPayload<RdmaConnectChannel>;

    struct PendingConnect {
        std::error_code error{};
    };

    // The connection to establish: connect() finishes it and hands back a session
    // that owns it.
    RdmaConnectChannel(NBIO::Net::RdmaConnector& connector, NBIO::Core::Multiplexer& multiplexer,
                       NBIO::Async::Scheduler& scheduler);
    ~RdmaConnectChannel() noexcept;

    NBIO::Async::Task<RdmaResult<void>> Connect(NBIO::Net::Address peer);

    // The operation this channel wants from the backend is a one-shot poll; the
    // payload carries only whether one is already out there.
    Payload& Submit();
    void Complete();

    void Park(NBIO::Async::Coroutine waiter) noexcept { waiter_ = std::move(waiter); }

    PendingConnect& job() noexcept { return job_; }

    const PendingConnect& job() const noexcept { return job_; }

    NBIO::Net::RdmaConnector& connector() noexcept { return connector_; }

   private:
    NBIO::Net::RdmaConnector& connector_;
    NBIO::Net::Address peer_{};
    PendingConnect job_{};
    NBIO::Async::Coroutine waiter_{};
    Payload payload_{};
};
}  // namespace NBIO::Net

#endif  // defined(NBIO_ENABLE_RDMA) && defined(__linux__)
