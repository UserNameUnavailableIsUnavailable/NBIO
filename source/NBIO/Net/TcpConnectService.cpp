#include <NBIO/Net/TcpConnectService.hpp>

#include <NBIO/Net/TcpConnector.hpp>
#include <NBIO/Async/Runtime.hpp>
#include <NBIO/Async/Runtime.hpp>
#include <NBIO/Net/TcpConnectChannel.hpp>
#include <memory>
#include <system_error>
#include <utility>

namespace NBIO::Net {
namespace {
// The connect body, as a plain coroutine whose channel is an ordinary local: the
// channel lives in this frame for the length of one connect, which is exactly as long
// as a connect channel is good for.
NBIO::Async::Task<Utility::expected<std::shared_ptr<TcpSessionService>, std::error_code>> ConnectOn(
    const NBIO::Net::Address* source, const NBIO::Net::Address& target,
    NBIO::Net::Address::Family family) {
    TcpConnectChannel channel{NBIO::Net::TcpConnector{family}, NBIO::Async::Runtime::multiplexer(), NBIO::Async::Runtime::scheduler()};
    auto connected = source != nullptr ? co_await channel.Connect(*source, target) : co_await channel.Connect(target);
    if (!connected) [[unlikely]] {
        co_return Utility::unexpected<std::error_code>(connected.error());
    }
    // The service is spent and the session is what is left: the connection goes
    // straight into it, and the channel that made it is destroyed with this frame.
    co_return std::make_shared<TcpSessionService>(std::move(*connected));
}
}  // namespace

TcpConnectService::TcpConnectService(NBIO::Net::Address::Family family) : family_(family) {}

NBIO::Async::Task<Utility::expected<std::shared_ptr<TcpSessionService>, std::error_code>> TcpConnectService::Connect(
    const NBIO::Net::Address& target) {
    return ConnectOn(nullptr, target, family_);
}

NBIO::Async::Task<Utility::expected<std::shared_ptr<TcpSessionService>, std::error_code>> TcpConnectService::Connect(
    const NBIO::Net::Address& source, const NBIO::Net::Address& target) {
    return ConnectOn(&source, target, family_);
}
}  // namespace NBIO::Net




