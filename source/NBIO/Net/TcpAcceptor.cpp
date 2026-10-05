#include <NBIO/Net/TcpAcceptor.hpp>

namespace NBIO::Net {
TcpAcceptor::TcpAcceptor(Address::Family family) : socket_(family, TcpSocket::Type::kStream) {}

Utility::expected<void, std::error_code> TcpAcceptor::Bind(const Address& address, std::size_t backlog) noexcept {
    if (auto result = socket_.Bind(address); !result) [[unlikely]] {
        return Utility::unexpected<std::error_code>(result.error());
    }
    return socket_.Listen(static_cast<int>(backlog));
}

Utility::expected<TcpConnector, std::error_code> TcpAcceptor::Accept() noexcept {
    auto result = socket_.Accept();
    if (!result) [[unlikely]] {
        return Utility::unexpected<std::error_code>(result.error());
    }
    return TcpConnector(std::move(result->first));
}

TcpConnector TcpAcceptor::Adopt(TcpSocket socket) const noexcept { return TcpConnector(std::move(socket)); }

Utility::expected<void, std::error_code> TcpAcceptor::ReuseAddress(bool toggle) noexcept { return socket_.ReuseAddress(toggle); }

Utility::expected<void, std::error_code> TcpAcceptor::NonBlocking(bool toggle) noexcept { return socket_.NonBlocking(toggle); }

void TcpAcceptor::Close() noexcept { socket_.Close(); }

bool TcpAcceptor::is_valid() const noexcept { return socket_.is_valid(); }
}  // namespace NBIO::Net
