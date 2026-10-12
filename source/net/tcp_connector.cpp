#include <nbio/net/tcp_connector.hpp>

namespace nbio::net {
TcpConnector::TcpConnector(Address::Family family) : Socket(family, Type::kStream) {}

TcpConnector::TcpConnector(Socket socket) noexcept : Socket(std::move(socket)) {}

utility::expected<void, std::error_code> TcpConnector::NonBlocking(bool toggle) noexcept {
    return ToggleNonBlocking(toggle);
}

utility::expected<void, std::error_code> TcpConnector::ReuseAddress(bool toggle) noexcept {
    return ToggleReuseAddress(toggle);
}
}  // namespace nbio::net
