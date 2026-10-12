#pragma once

#include <chrono>
#include <nbio/net/address.hpp>
#include <nbio/utility/expected.hpp>
#include <span>
#include <system_error>
#include <utility>

#if defined(__linux__)
#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>
#elif defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#error "Unsupported platform"
#endif

namespace nbio::net {
class Socket {
   public:
    constexpr static std::uintptr_t kInvalidHandle{static_cast<std::uintptr_t>(-1)};

    enum class Type {
        kRaw,
        kStream,
        kDatagram,
    };
    enum class Protocol {
        kDefault,
        kIPv4,
        kIPv6,
        kICMPv4,
        kICMPv6,
        kTCP,
        kUDP,
    };
    enum class ShutdownHow { kRead, kWrite, kBoth };

    Socket() noexcept = default;
    Socket(Address::Family family, Type type, Protocol protocol = Protocol::kDefault);
    Socket(const Socket&) = delete;
    Socket& operator=(const Socket&) = delete;
    Socket(Socket&& other) noexcept { std::swap(handle_, other.handle_); }
    Socket& operator=(Socket&& other) noexcept {
        std::swap(handle_, other.handle_);
        return *this;
    }
    ~Socket() noexcept { Close(); }

    [[nodiscard]] static Socket Adopt(std::uintptr_t handle) noexcept;

    std::uintptr_t native_handle() const noexcept { return handle_; }
    void swap(Socket& other) noexcept { std::swap(handle_, other.handle_); }
    bool is_valid() const noexcept { return handle_ != kInvalidHandle; }

    utility::expected<void, std::error_code> Bind(const Address& local) noexcept;
    utility::expected<void, std::error_code> Listen(int backlog = 4096) noexcept;
    utility::expected<std::pair<Socket, Address>, std::error_code> Accept() noexcept;
    utility::expected<std::size_t, std::error_code> Receive(std::span<char> buffer) noexcept;
    utility::expected<std::size_t, std::error_code> Send(std::span<const char> buffer) noexcept;
    utility::expected<void, std::error_code> Connect(const Address& peer) noexcept;

    utility::expected<void, std::error_code> TakeError() const noexcept;

    ///@brief Set the non-blocking status of the socket.
    ///@param toggle Whether to enable or disable non-blocking mode.
    ///@return An expected indicating success or failure.
    utility::expected<void, std::error_code> ToggleNonBlocking(bool toggle) noexcept;
    ///@brief Get the current non-blocking status of the socket.
    ///@return The non-blocking status, or an error (not supported on Windows).
    utility::expected<bool, std::error_code> IsNonBlocking() const noexcept;

    ///@brief Set the reuse-address option for the socket.
    ///@param toggle Whether to enable or disable the reuse-address option.
    ///@return An expected indicating success or failure.
    utility::expected<void, std::error_code> ToggleReuseAddress(bool toggle) noexcept;
    ///@brief Get the current reuse-address option for the socket.
    ///@return An expected containing the reuse-address status or an error code.
    utility::expected<bool, std::error_code> IsReuseAddress() const noexcept;

    ///@brief Set the reuse-port option for the socket.
    ///@param toggle Whether to enable or disable the reuse-port option.
    ///@return An expected indicating success or failure.
    utility::expected<void, std::error_code> ToggleReusePort(bool toggle) noexcept;
    ///@brief Get the current reuse-port option for the socket.
    ///@return An expected containing the reuse-port status or an error code.
    utility::expected<bool, std::error_code> IsReusePort() const noexcept;

    ///@brief Set the keep-alive option for the socket.
    ///@param toggle Whether to enable or disable the keep-alive option.
    ///@return An expected indicating success or failure.
    utility::expected<void, std::error_code> ToggleKeepAlive(bool toggle) noexcept;
    ///@brief Get the current keep-alive option for the socket.
    ///@return An expected containing the keep-alive status or an error code.
    utility::expected<bool, std::error_code> IsKeepAlive() const noexcept;

    ///@brief Set or get the TCP_NODELAY option for the socket.
    ///@param toggle Whether to enable or disable the TCP_NODELAY option.
    ///@return An expected indicating success or failure.
    utility::expected<void, std::error_code> ToggleNoDelay(bool toggle) noexcept;
    ///@brief Get the current TCP_NODELAY option for the socket.
    ///@return An expected containing the TCP_NODELAY status or an error code.
    utility::expected<bool, std::error_code> IsNoDelay() const noexcept;

    ///@brief Set or get the linger option for the socket.
    ///@param duration Nonnegative native-range seconds. If zero, disables linger.
    ///@return An expected indicating success or failure.
    utility::expected<void, std::error_code> SetLingerDuration(std::chrono::seconds duration) noexcept;
    ///@brief Get the current linger option for the socket.
    ///@return An expected containing the linger duration or an error code.
    utility::expected<std::chrono::seconds, std::error_code> GetLingerDuration() const noexcept;

    utility::expected<Address, std::error_code> GetLocalAddress() const noexcept;
    utility::expected<Address, std::error_code> GetRemoteAddress() const noexcept;

    void Shutdown(ShutdownHow how = ShutdownHow::kBoth) noexcept;
    void Close() noexcept;

   private:
    template <typename T>
    std::error_code SetNativeOption(int level, int option, const T& value) noexcept;
    template <typename T>
    utility::expected<T, std::error_code> GetNativeOption(int level, int option) const noexcept;
    static std::error_code last_error() noexcept;
    std::uintptr_t handle_{ kInvalidHandle };
};
}  // namespace nbio::net
