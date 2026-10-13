#pragma once

#include <nbio/net/address.hpp>
#include <nbio/net/socket.hpp>
#include <nbio/utility/expected.hpp>
#include <system_error>

namespace nbio::net {
class TcpConnector final : public Socket {
   public:
    explicit TcpConnector(Address::Family family = Address::Family::kIPv4);

    TcpConnector(const TcpConnector&) = delete;
    TcpConnector& operator=(const TcpConnector&) = delete;

    TcpConnector(TcpConnector&& other) noexcept = default;
    TcpConnector& operator=(TcpConnector&& other) noexcept = default;

    ~TcpConnector() noexcept = default;

    friend void swap(TcpConnector& left, TcpConnector& right) noexcept { left.swap(right); }

    utility::expected<void, std::error_code> Listen(int backlog = 4096) noexcept = delete;

    [[deprecated("Use ToggleNonBlocking instead")]]
    utility::expected<void, std::error_code> NonBlocking(bool toggle = true) noexcept;
    [[deprecated("Use ToggleReuseAddress instead")]]
    utility::expected<void, std::error_code> ReuseAddress(bool toggle = true) noexcept;

   private:
    friend class TcpListener;
    explicit TcpConnector(Socket socket) noexcept;
};
}  // namespace nbio::net
