#include <nbio/net/tcp_listener.hpp>

namespace nbio::net {
TcpListener::TcpListener(Address::Family family) : Socket(family, Type::kStream) {}

utility::expected<void, std::error_code> TcpListener::Bind(const Address& address, std::size_t backlog) noexcept {
    if (auto result = Socket::Bind(address); !result) [[unlikely]] {
        return utility::unexpected<std::error_code>(result.error());
    }
    return Listen(static_cast<int>(backlog));
}

utility::expected<TcpConnector, std::error_code> TcpListener::Accept() noexcept {
    auto result = Socket::Accept();
    if (!result) [[unlikely]] {
        return utility::unexpected<std::error_code>(result.error());
    }
    return TcpConnector(std::move(result->first));
}

TcpConnector TcpListener::Adopt(Socket socket) const noexcept { return TcpConnector(std::move(socket)); }

utility::expected<void, std::error_code> TcpListener::ReuseAddress(bool toggle) noexcept {
    return ToggleReuseAddress(toggle);
}

utility::expected<void, std::error_code> TcpListener::NonBlocking(bool toggle) noexcept {
    return ToggleNonBlocking(toggle);
}
}  // namespace nbio::net
