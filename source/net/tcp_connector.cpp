#include <nbio/net/tcp_connector.hpp>

namespace nbio::net {
TcpConnector::TcpConnector(Address::Family family) : TcpSocket(family, Type::kStream) {}

TcpConnector::TcpConnector(TcpSocket socket) noexcept : TcpSocket(std::move(socket)) {}

utility::expected<void, std::error_code> TcpConnector::Connect(const Address& peer) noexcept {
    if (auto started = StartConnect(peer); !started) [[unlikely]] {
        return started;
    }
    return FinishConnect();
}

utility::expected<void, std::error_code> TcpConnector::StartConnect(const Address& peer) noexcept {
    if (auto connected = TcpSocket::Connect(peer); !connected) [[unlikely]] {
        const std::error_code& why = connected.error();
        if (why != std::errc::operation_in_progress && why != std::errc::connection_already_in_progress) {
            return utility::unexpected<std::error_code>(why);
        }
    }
    return {};
}

utility::expected<void, std::error_code> TcpConnector::FinishConnect() noexcept {
    if (auto settled = TakeError(); !settled) [[unlikely]] {
        return settled;
    }
    return {};
}

utility::expected<void, std::error_code> TcpConnector::Shutdown(TcpSocket::ShutdownHow how) noexcept {
    TcpSocket::Shutdown(how);
    return {};
}

utility::expected<void, std::error_code> TcpConnector::NonBlocking(bool toggle) noexcept {
    return ToggleNonBlocking(toggle);
}

utility::expected<void, std::error_code> TcpConnector::ReuseAddress(bool toggle) noexcept {
    return ToggleReuseAddress(toggle);
}
}  // namespace nbio::net
