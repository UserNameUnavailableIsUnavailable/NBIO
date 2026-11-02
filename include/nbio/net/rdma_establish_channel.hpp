#pragma once
#if defined(NBIO_ENABLE_RDMA) && defined(__linux__)

#include <nbio/async/coroutine.hpp>
#include <nbio/async/scheduler.hpp>
#include <nbio/async/task.hpp>
#include <nbio/core/channel.hpp>
#include <nbio/net/payload.hpp>
#include <nbio/net/rdma_communication.hpp>
#include <system_error>
#include <utility>

namespace nbio::net {
/**
 * @brief The asynchronous half of finishing a connection: drives the prepared
 *        communication's CM fd until RDMA_CM_EVENT_ESTABLISHED arrives.
 */
class RdmaEstablishChannel final : public nbio::core::Channel<RdmaEstablishChannel> {
   public:
    using Handle = int;
    using Payload = detail::PollPayload<RdmaEstablishChannel>;

    struct PendingEstablish {
        std::error_code error{};
    };

    RdmaEstablishChannel(nbio::net::RdmaCommunication& communication, nbio::core::Multiplexer& multiplexer,
                         nbio::async::Scheduler& scheduler);
    ~RdmaEstablishChannel() noexcept;

    nbio::async::Task<RdmaResult<void>> Establish();

    Payload& Submit();
    void Complete();
    void Park(nbio::async::Coroutine waiter) noexcept { waiter_ = std::move(waiter); }

    PendingEstablish& job() noexcept { return job_; }
    const PendingEstablish& job() const noexcept { return job_; }

    // One non-blocking step: kNotReady until established.
    RdmaResult<void> Advance() noexcept;

   private:
    nbio::net::RdmaCommunication& communication_;
    PendingEstablish job_{};
    nbio::async::Coroutine waiter_{};
    Payload payload_{};
};
}  // namespace nbio::net

#endif  // defined(NBIO_ENABLE_RDMA) && defined(__linux__)
