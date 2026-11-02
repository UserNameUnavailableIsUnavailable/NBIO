#pragma once
#if defined(NBIO_ENABLE_RDMA) && defined(__linux__)

#include <nbio/async/task.hpp>
#include <nbio/net/rdma_communication.hpp>
#include <nbio/net/rdma_establish_channel.hpp>
#include <nbio/net/rdma_receive_channel.hpp>
#include <nbio/net/rdma_send_channel.hpp>

namespace nbio::net {
/**
 * @brief The interface half of a communication: a thin wrapper bundling the
 *        asynchronous channels of one connection with the data plane itself.
 *
 * @details Build it from a @ref RdmaCommunication returned by a listener or a
 * connector, then drive one connection end to end without touching the primitives.
 */
class RdmaSessionService {
   public:
    explicit RdmaSessionService(RdmaCommunication communication);

    RdmaSessionService(const RdmaSessionService&) = delete;
    RdmaSessionService& operator=(const RdmaSessionService&) = delete;
    RdmaSessionService(RdmaSessionService&&) = delete;
    RdmaSessionService& operator=(RdmaSessionService&&) = delete;
    ~RdmaSessionService() noexcept;

    nbio::async::Task<RdmaResult<void>> Establish();

    RdmaResult<RdmaSeq> SubmitSend(std::span<const char> buffer) noexcept;
    nbio::async::Task<RdmaResult<RdmaSeq>> WaitSend(RdmaSeq seq);
    RdmaResult<RdmaSendResult> PeekSend(RdmaSeq seq) noexcept;
    void CompleteSend(RdmaSeq seq) noexcept;

    RdmaResult<RdmaSeq> SubmitReceive(std::span<char> buffer) noexcept;
    nbio::async::Task<RdmaResult<RdmaSeq>> WaitReceive(RdmaSeq seq);
    RdmaResult<RdmaReceiveResult> PeekReceive(RdmaSeq seq) noexcept;
    void CompleteReceive(RdmaSeq seq) noexcept;

    RdmaCommunication& communication() noexcept { return communication_; }
    const RdmaCommunication& communication() const noexcept { return communication_; }

   private:
    RdmaCommunication communication_;
    RdmaSendChannel send_channel_;
    RdmaReceiveChannel receive_channel_;
    RdmaEstablishChannel establish_channel_;
};
}  // namespace nbio::net

#endif  // defined(NBIO_ENABLE_RDMA) && defined(__linux__)
