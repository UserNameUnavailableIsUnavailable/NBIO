#pragma once

#include <cstdint>
#include <system_error>
#include <utility>

#include <NBIO/Utility/Expected.hpp>
#include <NBIO/Net/Address.hpp>
#include <NBIO/Net/TcpConnector.hpp>
#include <NBIO/Net/TcpSocket.hpp>

namespace NBIO::Net {
class TcpAcceptor {
   public:
    explicit TcpAcceptor(Address::Family family = Address::Family::kIPv4);

    TcpAcceptor(const TcpAcceptor&) = delete;
    TcpAcceptor& operator=(const TcpAcceptor&) = delete;

    Utility::expected<void, std::error_code> Bind(const Address& address, std::size_t backlog = 4096) noexcept;
    Utility::expected<void, std::error_code> ReuseAddress(bool toggle = true) noexcept;
    Utility::expected<TcpConnector, std::error_code> Accept() noexcept;
    TcpConnector Adopt(TcpSocket socket) const noexcept;
    Utility::expected<void, std::error_code> NonBlocking(bool toggle = true) noexcept;

    std::uintptr_t native_handle() const noexcept { return socket_.native_handle(); }

    void Close() noexcept;
    bool is_valid() const noexcept;

   private:
    TcpSocket socket_;
};
}  // namespace NBIO::Net
