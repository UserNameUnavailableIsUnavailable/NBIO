#if defined(NBIO_ENABLE_RDMA) && defined(__linux__)

#include <nbio/net/rdma_session_service.hpp>
#include <nbio/runtime/daemon.hpp>

namespace nbio::net {
RdmaSessionService::RdmaSessionService(RdmaCommunication communication)
    : communication_(std::move(communication)),
      send_channel_(communication_, nbio::runtime::Daemon::multiplexer(), nbio::runtime::Daemon::scheduler()),
      receive_channel_(communication_, nbio::runtime::Daemon::multiplexer(), nbio::runtime::Daemon::scheduler()),
      establish_channel_(communication_, nbio::runtime::Daemon::multiplexer(), nbio::runtime::Daemon::scheduler()) {}

RdmaSessionService::~RdmaSessionService() noexcept = default;

nbio::async::Task<RdmaResult<void>> RdmaSessionService::Establish() {
    co_return co_await establish_channel_.Establish();
}

RdmaResult<RdmaSeq> RdmaSessionService::SubmitSend(std::span<const char> buffer) noexcept {
    return send_channel_.SubmitSend(buffer);
}

nbio::async::Task<RdmaResult<RdmaSeq>> RdmaSessionService::WaitSend(RdmaSeq seq) {
    co_return co_await send_channel_.WaitSend(seq);
}

RdmaResult<RdmaSendResult> RdmaSessionService::PeekSend(RdmaSeq seq) noexcept {
    return send_channel_.PeekSend(seq);
}

void RdmaSessionService::CompleteSend(RdmaSeq seq) noexcept { send_channel_.CompleteSend(seq); }

RdmaResult<RdmaSeq> RdmaSessionService::SubmitReceive(std::span<char> buffer) noexcept {
    return receive_channel_.SubmitReceive(buffer);
}

nbio::async::Task<RdmaResult<RdmaSeq>> RdmaSessionService::WaitReceive(RdmaSeq seq) {
    co_return co_await receive_channel_.WaitReceive(seq);
}

RdmaResult<RdmaReceiveResult> RdmaSessionService::PeekReceive(RdmaSeq seq) noexcept {
    return receive_channel_.PeekReceive(seq);
}

void RdmaSessionService::CompleteReceive(RdmaSeq seq) noexcept { receive_channel_.CompleteReceive(seq); }
}  // namespace nbio::net

#endif  // defined(NBIO_ENABLE_RDMA) && defined(__linux__)
