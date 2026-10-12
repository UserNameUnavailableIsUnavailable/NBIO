#pragma once

#include <nbio/net/address.hpp>
#include <nbio/net/tcp_socket.hpp>
#include <nbio/utility/expected.hpp>
#include <system_error>

namespace nbio::net {
class TcpConnector final : public TcpSocket {
   public:
    explicit TcpConnector(Address::Family family = Address::Family::kIPv4);

    TcpConnector(const TcpConnector&) = delete;
    TcpConnector& operator=(const TcpConnector&) = delete;

    TcpConnector(TcpConnector&& other) noexcept = default;
    TcpConnector& operator=(TcpConnector&& other) noexcept = default;

    ~TcpConnector() noexcept = default;

    friend void swap(TcpConnector& left, TcpConnector& right) noexcept { left.swap(right); }

    utility::expected<void, std::error_code> Listen(int backlog = 4096) noexcept = delete;
    utility::expected<void, std::error_code> Connect(const Address& peer) noexcept;
    utility::expected<void, std::error_code> StartConnect(const Address& peer) noexcept;
    utility::expected<void, std::error_code> FinishConnect() noexcept;
    utility::expected<void, std::error_code> Shutdown(
        TcpSocket::ShutdownHow how = TcpSocket::ShutdownHow::kBoth) noexcept;

    [[deprecated("Use is_valid instead")]]
    bool IsValid() const noexcept { return is_valid(); }

    [[deprecated("Use ToggleNonBlocking instead")]]
    utility::expected<void, std::error_code> NonBlocking(bool toggle = true) noexcept;
    [[deprecated("Use ToggleReuseAddress instead")]]
    utility::expected<void, std::error_code> ReuseAddress(bool toggle = true) noexcept;

   private:
    friend class TcpAcceptor;
    explicit TcpConnector(TcpSocket socket) noexcept;
};
}  // namespace nbio::net
