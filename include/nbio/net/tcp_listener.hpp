#pragma once

#include <cstdint>
#include <nbio/net/address.hpp>
#include <nbio/net/tcp_connector.hpp>
#include <nbio/net/socket.hpp>
#include <nbio/utility/expected.hpp>
#include <system_error>
#include <utility>

namespace nbio::net {
class TcpListener final : public Socket {
   public:
    explicit TcpListener(Address::Family family = Address::Family::kIPv4);

    TcpListener(const TcpListener&) = delete;
    TcpListener& operator=(const TcpListener&) = delete;
    TcpListener(TcpListener&& other) noexcept = default;
    TcpListener& operator=(TcpListener&& other) noexcept = default;

    utility::expected<void, std::error_code> Connect(const Address& peer) noexcept = delete;
    utility::expected<void, std::error_code> Bind(const Address& address, std::size_t backlog = 4096) noexcept;
    [[deprecated("Use ToggleReuseAddress instead")]]
    utility::expected<void, std::error_code> ReuseAddress(bool toggle = true) noexcept;
    utility::expected<TcpConnector, std::error_code> Accept() noexcept;
    TcpConnector Adopt(Socket socket) const noexcept;
    [[deprecated("Use ToggleNonBlocking instead")]]
    utility::expected<void, std::error_code> NonBlocking(bool toggle = true) noexcept;
};
}  // namespace nbio::net
