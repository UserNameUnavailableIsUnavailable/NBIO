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
 * @brief The asynchronous half of a communication's send queue.
 *
 * @details Submitting, peeking, and completing are synchronous; only waiting for a
 * sequence to be reaped parks the coroutine on the send completion channel.
 */
class RdmaSendChannel final : public nbio::core::Channel<RdmaSendChannel> {
   public:
    using Handle = int;
    using Payload = detail::PollPayload<RdmaSendChannel>;

    struct PendingSend {
        RdmaSeq seq{0};
        RdmaSeq mark{0};
        std::error_code error{};
    };

    RdmaSendChannel(nbio::net::RdmaCommunication& communication, nbio::core::Multiplexer& multiplexer,
                    nbio::async::Scheduler& scheduler);
    ~RdmaSendChannel() noexcept;

    RdmaResult<RdmaSeq> SubmitSend(std::span<const char> buffer) noexcept;
    nbio::async::Task<RdmaResult<RdmaSeq>> WaitSend(RdmaSeq seq);
    RdmaResult<RdmaSendResult> PeekSend(RdmaSeq seq) noexcept;
    void CompleteSend(RdmaSeq seq) noexcept;

    Payload& Submit();
    void Complete();
    void Park(nbio::async::Coroutine waiter) noexcept { waiter_ = std::move(waiter); }

    PendingSend& job() noexcept { return job_; }
    const PendingSend& job() const noexcept { return job_; }
    nbio::net::RdmaCommunication& communication() noexcept { return communication_; }

   private:
    nbio::net::RdmaCommunication& communication_;
    PendingSend job_{};
    nbio::async::Coroutine waiter_{};
    Payload payload_{};
};
}  // namespace nbio::net

#endif  // defined(NBIO_ENABLE_RDMA) && defined(__linux__)
