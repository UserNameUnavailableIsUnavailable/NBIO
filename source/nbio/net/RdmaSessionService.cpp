#if defined(NBIO_ENABLE_RDMA) && defined(__linux__)
#include <nbio/net/RdmaSessionService.hpp>

namespace nbio::net {
RdmaSessionService::RdmaSessionService(nbio::net::RdmaConnector connection, nbio::Core::Multiplexer& multiplexer,
                                       nbio::async::Scheduler& scheduler)
    : connection_(std::move(connection)),
      send_channel_(connection_, multiplexer, scheduler),
      receive_channel_(connection_, multiplexer, scheduler) {}

RdmaResult<void> RdmaSessionService::Send(std::span<char> chunk, std::size_t length) noexcept {
    return send_channel_.Send(chunk, length);
}

nbio::async::Task<RdmaResult<std::size_t>> RdmaSessionService::PollSend(std::size_t count) {
    co_return co_await send_channel_.Poll(count);
}

nbio::async::Task<RdmaResult<RdmaReceiveResult>> RdmaSessionService::Receive() {
    co_return co_await receive_channel_.Receive();
}

nbio::async::Task<RdmaResult<RdmaReceiveResult>> RdmaSessionService::TryReceive() {
    co_return co_await receive_channel_.TryReceive();
}

RdmaResult<void> RdmaSessionService::Release(std::span<char> chunk) noexcept {
    return receive_channel_.Release(chunk);
}

nbio::net::RdmaConnector& RdmaSessionService::connection() noexcept { return connection_; }

const nbio::net::RdmaConnector& RdmaSessionService::connection() const noexcept { return connection_; }

RdmaSendChannel& RdmaSessionService::send_channel() noexcept { return send_channel_; }

const RdmaSendChannel& RdmaSessionService::send_channel() const noexcept { return send_channel_; }

RdmaReceiveChannel& RdmaSessionService::receive_channel() noexcept { return receive_channel_; }

const RdmaReceiveChannel& RdmaSessionService::receive_channel() const noexcept { return receive_channel_; }
}  // namespace nbio::net
#endif  // defined(NBIO_ENABLE_RDMA) && defined(__linux__)