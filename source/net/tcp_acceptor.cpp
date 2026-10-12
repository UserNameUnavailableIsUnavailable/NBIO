#include <nbio/net/tcp_acceptor.hpp>

namespace nbio::net {
TcpAcceptor::TcpAcceptor(Address::Family family) : TcpSocket(family, Type::kStream) {}

utility::expected<void, std::error_code> TcpAcceptor::Bind(const Address& address, std::size_t backlog) noexcept {
    if (auto result = TcpSocket::Bind(address); !result) [[unlikely]] {
        return utility::unexpected<std::error_code>(result.error());
    }
    return Listen(static_cast<int>(backlog));
}

utility::expected<TcpConnector, std::error_code> TcpAcceptor::Accept() noexcept {
    auto result = TcpSocket::Accept();
    if (!result) [[unlikely]] {
        return utility::unexpected<std::error_code>(result.error());
    }
    return TcpConnector(std::move(result->first));
}

TcpConnector TcpAcceptor::Adopt(TcpSocket socket) const noexcept { return TcpConnector(std::move(socket)); }

utility::expected<void, std::error_code> TcpAcceptor::ReuseAddress(bool toggle) noexcept {
    return ToggleReuseAddress(toggle);
}

utility::expected<void, std::error_code> TcpAcceptor::NonBlocking(bool toggle) noexcept {
    return ToggleNonBlocking(toggle);
}
}  // namespace nbio::net
