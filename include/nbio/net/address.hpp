#pragma once

#include <cstdint>
#include <nbio/utility.hpp>
#include <string>
#include <string_view>
#include <system_error>

#if defined(__linux__)
#include <arpa/inet.h>
#include <sys/socket.h>
#elif defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#error "Unsupported platform"
#endif

namespace nbio::net {
class Address {
   public:
    enum class Family { kUnspecified, kIPv4, kIPv6 };

    Address() noexcept = default;

    ///@brief Parse an IP address from a string and port, automatically detecting the family.
    ///@param ip_str The IP address as a string.
    ///@param port The port number in host byte order.
    ///@return An expected containing the Address if successful, or an error code otherwise.
    static utility::expected<Address, std::error_code> FromUnknown(std::string_view ip_str, std::uint16_t port) {
        // guess
        if (auto result = FromIPv4(ip_str, port); result) {
            return result;
        } else if (result.error() != std::errc::invalid_argument) {
            return result;
        }
        return FromIPv6(ip_str, port);
    }
    ///@brief Parse an IPv4 address from a string and port.
    ///@param ip_str The IPv4 address as a string.
    ///@param port The port number in host byte order.
    ///@return An expected containing the Address if successful, or an error code otherwise.
    static utility::expected<Address, std::error_code> FromIPv4(std::string_view ip_str, std::uint16_t port);
    ///@brief Parse an IPv6 address from a string and port.
    ///@param ip_str The IPv6 address as a string.
    ///@param port The port number in host byte order.
    ///@return An expected containing the Address if successful, or an error code otherwise.
    static utility::expected<Address, std::error_code> FromIPv6(std::string_view ip_str, std::uint16_t port);

    [[deprecated("Use FromIPv4(ip_str, port) instead")]]
    static Address FromV4(std::string_view ip_str, std::uint16_t port);
    [[deprecated("Use FromIPv6(ip_str, port) instead")]]
    static Address FromV6(std::string_view ip_str, std::uint16_t port);

    Family family() const noexcept;
    bool is_valid() const noexcept { return storage_.ss_family != AF_UNSPEC; }

    ///@brief Get the IP address as a string.
    ///@return An expected containing the IP address if successful, or an error code otherwise.
    ///@note This function fails only when the underlying address family is given an unsupported value via raw storage.
    utility::expected<std::string, std::error_code> ip() const noexcept;
    ///@brief Get the port number.
    ///@return An expected containing the port number if successful, or an error code otherwise.
    ///@note This function fails only when the underlying address family is given an unsupported value via raw storage.
    utility::expected<std::uint16_t, std::error_code> port() const noexcept;

    template <typename T = ::sockaddr>
    T* storage() noexcept {
        return reinterpret_cast<T*>(&storage_);
    }

    template <typename T = ::sockaddr>
    const T* storage() const noexcept {
        return reinterpret_cast<const T*>(&storage_);
    }

    ::socklen_t& length() noexcept { return length_; }
    const ::socklen_t& length() const noexcept { return length_; }
    static constexpr ::socklen_t capacity() noexcept { return sizeof(storage_); }

   private:
    ::sockaddr_storage storage_{};
    ::socklen_t length_{0};
};
}  // namespace nbio::net
