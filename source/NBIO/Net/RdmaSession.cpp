#if defined(NBIO_ENABLE_RDMA) && defined(__linux__)
#include <NBIO/Net/RdmaSession.hpp>

namespace NBIO::Net {
RdmaSessionService::RdmaSessionService(NBIO::Net::RdmaConnector connection, NBIO::Core::Multiplexer& multiplexer,
                                       NBIO::Async::Scheduler& scheduler)
    : connection_(std::move(connection)),
      send_channel_(connection_, multiplexer, scheduler),
      receive_channel_(connection_, multiplexer, scheduler) {}

NBIO::Utility::expected<void, std::string> RdmaSessionService::Send(std::span<char> chunk, std::size_t length) noexcept {
    return send_channel_.Send(chunk, length);
}

NBIO::Async::Task<NBIO::Utility::expected<std::size_t, std::string>> RdmaSessionService::PollSend(std::size_t count) {
    co_return co_await send_channel_.Poll(count);
}

NBIO::Async::Task<NBIO::Utility::expected<std::optional<std::span<char>>, std::string>> RdmaSessionService::Receive() {
    co_return co_await receive_channel_.Receive();
}

NBIO::Async::Task<NBIO::Utility::expected<std::optional<std::span<char>>, std::string>> RdmaSessionService::TryReceive() {
    co_return co_await receive_channel_.TryReceive();
}

NBIO::Utility::expected<void, std::string> RdmaSessionService::Release(std::span<char> chunk) noexcept {
    return receive_channel_.Release(chunk);
}

NBIO::Net::RdmaConnector& RdmaSessionService::connection() noexcept { return connection_; }

const NBIO::Net::RdmaConnector& RdmaSessionService::connection() const noexcept { return connection_; }

RdmaSendChannel& RdmaSessionService::send_channel() noexcept { return send_channel_; }

const RdmaSendChannel& RdmaSessionService::send_channel() const noexcept { return send_channel_; }

RdmaReceiveChannel& RdmaSessionService::receive_channel() noexcept { return receive_channel_; }

const RdmaReceiveChannel& RdmaSessionService::receive_channel() const noexcept { return receive_channel_; }
}  // namespace NBIO::Net

#endif  // defined(NBIO_ENABLE_RDMA) && defined(__linux__)
