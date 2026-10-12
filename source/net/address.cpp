#include <cerrno>
#include <nbio/net/address.hpp>
#include <stdexcept>

namespace nbio::net {
Address Address::FromV4(std::string_view ip, std::uint16_t port) {
    auto result = FromIPv4(ip, port);
    if (!result) {
        if (result.error() == std::errc::invalid_argument) {
            throw std::invalid_argument("Address::FromV4: invalid IPv4 address");
        }
        throw std::system_error(result.error(), "Address::FromV4: inet_pton failed");
    }
    return *result;
}

Address Address::FromV6(std::string_view ip, std::uint16_t port) {
    auto result = FromIPv6(ip, port);
    if (!result) {
        if (result.error() == std::errc::invalid_argument) {
            throw std::invalid_argument("Address::FromV6: invalid IPv6 address");
        }
        throw std::system_error(result.error(), "Address::FromV6: inet_pton failed");
    }
    return *result;
}

utility::expected<Address, std::error_code> Address::FromIPv4(std::string_view ip, std::uint16_t port) {
    if (ip.find('\0') != std::string_view::npos) {
        return utility::unexpected<std::error_code>(std::make_error_code(std::errc::invalid_argument));
    }
    Address address;
    auto& v4 = *reinterpret_cast<::sockaddr_in*>(&address.storage_);
    v4.sin_family = AF_INET;
    v4.sin_port = htons(port);
    const int result = ::inet_pton(AF_INET, std::string(ip).c_str(), &v4.sin_addr);
    if (result == 0) {
        return utility::unexpected<std::error_code>(std::make_error_code(std::errc::invalid_argument));
    }
    if (result < 0) {
#if defined(__linux__)
        return utility::unexpected<std::error_code>({errno, std::system_category()});
#elif defined(_WIN32)
        return utility::unexpected<std::error_code>({::WSAGetLastError(), std::system_category()});
#endif
    }
    address.length_ = sizeof(::sockaddr_in);
    return address;
}

utility::expected<Address, std::error_code> Address::FromIPv6(std::string_view ip, std::uint16_t port) {
    if (ip.find('\0') != std::string_view::npos) {
        return utility::unexpected<std::error_code>(std::make_error_code(std::errc::invalid_argument));
    }
    Address address;
    auto& v6 = *reinterpret_cast<::sockaddr_in6*>(&address.storage_);
    v6.sin6_family = AF_INET6;
    v6.sin6_port = htons(port);
    const int result = ::inet_pton(AF_INET6, std::string(ip).c_str(), &v6.sin6_addr);
    if (result == 0) {
        return utility::unexpected<std::error_code>(std::make_error_code(std::errc::invalid_argument));
    }
    if (result < 0) {
#if defined(__linux__)
        return utility::unexpected<std::error_code>({errno, std::system_category()});
#elif defined(_WIN32)
        return utility::unexpected<std::error_code>({::WSAGetLastError(), std::system_category()});
#endif
    }
    address.length_ = sizeof(::sockaddr_in6);
    return address;
}

Address::Family Address::family() const noexcept {
    switch (storage_.ss_family) {
        case AF_INET:
            return Family::kIPv4;
        case AF_INET6:
            return Family::kIPv6;
        default:
            return Family::kUnspecified;
    }
}

utility::expected<std::string, std::error_code> Address::ip() const noexcept {
    if (storage_.ss_family == AF_INET) {
        char buffer[INET_ADDRSTRLEN] = {};
        const auto* v4 = reinterpret_cast<const ::sockaddr_in*>(&storage_);
        if (::inet_ntop(AF_INET, &v4->sin_addr, buffer, sizeof(buffer)) == nullptr) {
            return utility::unexpected<std::error_code>({errno, std::system_category()});
        }
        return buffer;
    }
    if (storage_.ss_family == AF_INET6) {
        char buffer[INET6_ADDRSTRLEN] = {};
        const auto* v6 = reinterpret_cast<const ::sockaddr_in6*>(&storage_);
        if (::inet_ntop(AF_INET6, &v6->sin6_addr, buffer, sizeof(buffer)) == nullptr) {
            return utility::unexpected<std::error_code>({errno, std::system_category()});
        }
        return buffer;
    }
    return utility::unexpected<std::error_code>(std::make_error_code(std::errc::protocol_not_supported));
}

utility::expected<std::uint16_t, std::error_code> Address::port() const noexcept {
    if (storage_.ss_family == AF_INET) {
        return ntohs(reinterpret_cast<const ::sockaddr_in*>(&storage_)->sin_port);
    }
    if (storage_.ss_family == AF_INET6) {
        return ntohs(reinterpret_cast<const ::sockaddr_in6*>(&storage_)->sin6_port);
    }
    return utility::unexpected<std::error_code>(std::make_error_code(std::errc::protocol_not_supported));
}
}  // namespace nbio::net
