#pragma once

#include <cstdint>
#include <span>
#include <system_error>
#include <utility>

#include <NBIO/Utility/Expected.hpp>
#include <NBIO/Net/Address.hpp>
#include <NBIO/Net/TcpSocket.hpp>

namespace NBIO::Net {
class TcpConnector {
   public:
    explicit TcpConnector(Address::Family family = Address::Family::kIPv4);

    TcpConnector(const TcpConnector&) = delete;
    TcpConnector& operator=(const TcpConnector&) = delete;

    TcpConnector(TcpConnector&& other) noexcept = default;
    TcpConnector& operator=(TcpConnector&& other) noexcept = default;

    ~TcpConnector() noexcept = default;

    friend void swap(TcpConnector& left, TcpConnector& right) noexcept { left.socket_.swap(right.socket_); }

    Utility::expected<void, std::error_code> Bind(const Address& local) noexcept;
    Utility::expected<void, std::error_code> Connect(const Address& peer) noexcept;
    Utility::expected<void, std::error_code> StartConnect(const Address& peer) noexcept;
    Utility::expected<void, std::error_code> FinishConnect() noexcept;
    Utility::expected<std::size_t, std::error_code> Send(std::span<const char> buffer) noexcept;
    Utility::expected<std::size_t, std::error_code> Receive(std::span<char> buffer) noexcept;
    Utility::expected<void, std::error_code> Shutdown(TcpSocket::ShutdownHow how = TcpSocket::ShutdownHow::kBoth) noexcept;

    void Close() noexcept;
    bool IsValid() const noexcept { return socket_.is_valid(); }
    std::uintptr_t native_handle() const noexcept { return socket_.native_handle(); }

    Utility::expected<void, std::error_code> NonBlocking(bool toggle = true) noexcept;
    Utility::expected<void, std::error_code> ReuseAddress(bool toggle = true) noexcept;

   private:
    friend class TcpAcceptor;
    explicit TcpConnector(TcpSocket socket) noexcept;

    TcpSocket socket_;
};
}  // namespace NBIO::Net
