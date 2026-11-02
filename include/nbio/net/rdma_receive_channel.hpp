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
 * @brief The asynchronous half of a communication's receive queue.
 *
 * @details Submitting, peeking, and completing are synchronous; only waiting for a
 * sequence to be reaped parks the coroutine on the receive completion channel.
 */
class RdmaReceiveChannel final : public nbio::core::Channel<RdmaReceiveChannel> {
   public:
    using Handle = int;
    using Payload = detail::PollPayload<RdmaReceiveChannel>;

    struct PendingReceive {
        RdmaSeq seq{0};
        RdmaSeq mark{0};
        std::error_code error{};
    };

    RdmaReceiveChannel(nbio::net::RdmaCommunication& communication, nbio::core::Multiplexer& multiplexer,
                       nbio::async::Scheduler& scheduler);
    ~RdmaReceiveChannel() noexcept;

    RdmaResult<RdmaSeq> SubmitReceive(std::span<char> buffer) noexcept;
    nbio::async::Task<RdmaResult<RdmaSeq>> WaitReceive(RdmaSeq seq);
    RdmaResult<RdmaReceiveResult> PeekReceive(RdmaSeq seq) noexcept;
    void CompleteReceive(RdmaSeq seq) noexcept;

    Payload& Submit();
    void Complete();
    void Park(nbio::async::Coroutine waiter) noexcept { waiter_ = std::move(waiter); }

    PendingReceive& job() noexcept { return job_; }
    const PendingReceive& job() const noexcept { return job_; }
    nbio::net::RdmaCommunication& communication() noexcept { return communication_; }

   private:
    nbio::net::RdmaCommunication& communication_;
    PendingReceive job_{};
    nbio::async::Coroutine waiter_{};
    Payload payload_{};
};
}  // namespace nbio::net

#endif  // defined(NBIO_ENABLE_RDMA) && defined(__linux__)
