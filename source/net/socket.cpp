#include <cerrno>
#include <limits>
#include <nbio/net/socket.hpp>

#if defined(__linux__)
#include <netinet/tcp.h>
#elif defined(_WIN32)
#include <winsock2.h>
#endif

#if defined(__linux__)
#define IS_SOCKET_ERROR_AGAIN (errno == EAGAIN || errno == EWOULDBLOCK)
#elif defined(_WIN32)
#define IS_SOCKET_ERROR_AGAIN (::WSAGetLastError() == WSAEWOULDBLOCK)
#endif

#if defined(__linux__)
#define IS_SOCKET_ERROR_INTERRUPTED (errno == EINTR)
#elif defined(_WIN32)
#define IS_SOCKET_ERROR_INTERRUPTED (::WSAGetLastError() == WSAEINTR)
#endif

#include <system_error>

namespace nbio::net {
namespace {
#if defined(_WIN32)
int& WinsockRefs() {
    static int refs = 0;
    return refs;
}

void EnsureWinsock() {
    int& refs = WinsockRefs();
    if (refs == 0) {
        ::WSADATA data{};
        if (::WSAStartup(MAKEWORD(2, 2), &data) != 0) {
            throw std::system_error(std::error_code(static_cast<int>(::WSAGetLastError()), std::system_category()),
                                    "WSAStartup failed");
        }
    }
    ++refs;
}

void ReleaseWinsock() {
    int& refs = WinsockRefs();
    if (refs > 0 && --refs == 0) {
        ::WSACleanup();
    }
}
#else
void EnsureWinsock() {}
void ReleaseWinsock() {}
#endif
}  // namespace

std::error_code Socket::last_error() noexcept {
#if defined(__linux__)
    return std::error_code(errno, std::system_category());
#elif defined(_WIN32)
    return std::error_code(static_cast<int>(::WSAGetLastError()), std::system_category());
#endif
}

Socket::Socket(Address::Family family, Type type, Protocol protocol) {
    EnsureWinsock();

    int domain = 0;
    int type_ = 0;
    switch (family) {
        case Address::Family::kIPv4:
            domain = AF_INET;
            break;
        case Address::Family::kIPv6:
            domain = AF_INET6;
            break;
        default:
            throw std::system_error(std::make_error_code(std::errc::address_family_not_supported),
                                    "Socket: unsupported address family");
    }
    switch (type) {
        case Type::kStream:
            type_ = SOCK_STREAM;
            break;
        case Type::kDatagram:
            type_ = SOCK_DGRAM;
            break;
        default:
            throw std::system_error(std::make_error_code(std::errc::protocol_not_supported),
                                    "Socket: unsupported type");
    }

    int protocol_ = 0;
    switch (protocol) {
        case Protocol::kIPv4:
            protocol_ = IPPROTO_IP;
            break;
        case Protocol::kIPv6:
            protocol_ = IPPROTO_IPV6;
            break;
        case Protocol::kICMPv4:
            protocol_ = IPPROTO_ICMP;
            break;
        case Protocol::kTCP:
            protocol_ = IPPROTO_TCP;
            break;
        case Protocol::kUDP:
            protocol_ = IPPROTO_UDP;
            break;
        case Protocol::kICMPv6:
            protocol_ = IPPROTO_ICMPV6;
            break;
        case Protocol::kDefault:
            protocol_ = 0;
            break;
    }
    handle_ = ::socket(domain, type_, protocol_);
    if (handle_ == kInvalidHandle) {
        ReleaseWinsock();
        throw std::system_error(last_error(), "Socket: failed to create socket");
    }
#if defined(__linux__)
    ::fcntl(handle_, F_SETFD, FD_CLOEXEC);
#endif
}

Socket Socket::Adopt(std::uintptr_t handle) noexcept {
    if (handle == kInvalidHandle) {
        return Socket{};
    }
    EnsureWinsock();
    Socket socket;
    socket.handle_ = handle;
    return socket;
}

utility::expected<void, std::error_code> Socket::Bind(const Address& local) noexcept {
    if (::bind(handle_, local.storage<sockaddr>(), local.length()) < 0) {
        return utility::unexpected(last_error());
    }
    return {};
}

utility::expected<void, std::error_code> Socket::Listen(int backlog) noexcept {
    if (::listen(handle_, backlog) < 0) {
        return utility::unexpected<std::error_code>(last_error());
    }
    return {};
}

utility::expected<std::pair<Socket, Address>, std::error_code> Socket::Accept() noexcept {
    Address address;
    while (true) {
        address.length() = Address::capacity();
        auto handle = ::accept(handle_, address.storage<sockaddr>(), &address.length());

        if (static_cast<std::uintptr_t>(handle) != kInvalidHandle) {
            return std::pair{Socket::Adopt(handle), address};
        }
        if (IS_SOCKET_ERROR_AGAIN) {
            return utility::unexpected<std::error_code>(std::make_error_code(std::errc::operation_would_block));
        }
        if (IS_SOCKET_ERROR_INTERRUPTED) {
            continue;
        }
        return utility::unexpected<std::error_code>(last_error());
    }
}

utility::expected<std::size_t, std::error_code> Socket::Receive(std::span<char> buffer) noexcept {
    while (true) {
        const auto n = ::recv(handle_, buffer.data(), buffer.size(), 0);
        if (n > 0) {
            return static_cast<std::size_t>(n);
        }
        if (n == 0) {
            return 0;
        }
        if (IS_SOCKET_ERROR_AGAIN) {
            return utility::unexpected<std::error_code>(std::make_error_code(std::errc::operation_would_block));
        }
        if (IS_SOCKET_ERROR_INTERRUPTED) {
            continue;
        }
        return utility::unexpected<std::error_code>(last_error());
    }
}

utility::expected<std::size_t, std::error_code> Socket::Send(std::span<const char> buffer) noexcept {
    while (true) {
        const auto n = ::send(handle_, buffer.data(), buffer.size(), MSG_NOSIGNAL);
        if (n >= 0) {
            return static_cast<std::size_t>(n);
        }
        if (IS_SOCKET_ERROR_AGAIN) {
            return utility::unexpected<std::error_code>(std::make_error_code(std::errc::operation_would_block));
        }
        if (IS_SOCKET_ERROR_INTERRUPTED) {
            continue;
        }
        return utility::unexpected<std::error_code>(last_error());
    }
}

utility::expected<void, std::error_code> Socket::Connect(const Address& peer) noexcept {
    if (::connect(handle_, peer.storage<sockaddr>(), peer.length()) < 0) {
        return utility::unexpected<std::error_code>(last_error());
    }
    return {};
}

utility::expected<void, std::error_code> Socket::ToggleNonBlocking(bool toggle) noexcept {
#if defined(__linux__)
    const int flags = ::fcntl(handle_, F_GETFL, 0);
    if (flags < 0) {
        return utility::unexpected<std::error_code>(last_error());
    }
    const int updated = toggle ? (flags | O_NONBLOCK) : (flags & ~O_NONBLOCK);
    if (::fcntl(handle_, F_SETFL, updated) < 0) {
        return utility::unexpected<std::error_code>(last_error());
    }
#elif defined(_WIN32)
    u_long mode = toggle ? 1 : 0;
    if (::ioctlsocket(handle_, FIONBIO, &mode) != 0) {
        return utility::unexpected<std::error_code>(last_error());
    }
#endif
    return {};
}

utility::expected<bool, std::error_code> Socket::IsNonBlocking() const noexcept {
#if defined(__linux__)
    const int flags = ::fcntl(handle_, F_GETFL, 0);
    if (flags < 0) {
        return utility::unexpected<std::error_code>(last_error());
    }
    return (flags & O_NONBLOCK) != 0;
#elif defined(_WIN32)
    return utility::unexpected<std::error_code>(std::make_error_code(std::errc::operation_not_supported));
#endif
}

utility::expected<void, std::error_code> Socket::TakeError() const noexcept {
    auto pending = GetNativeOption<int>(SOL_SOCKET, SO_ERROR);
    if (!pending) {
        return utility::unexpected<std::error_code>(pending.error());
    }
    if (*pending != 0) {
        return utility::unexpected<std::error_code>({*pending, std::system_category()});
    }
    return {};
}

template <typename T>
std::error_code Socket::SetNativeOption(int level, int option, const T& value) noexcept {
#if defined(_WIN32)
    if (::setsockopt(handle_, level, option, reinterpret_cast<const char*>(&value), static_cast<int>(sizeof(T))) < 0)
#elif defined(__linux__)
    if (::setsockopt(handle_, level, option, &value, static_cast< ::socklen_t>(sizeof(T))) < 0)
#endif
    {
        return last_error();
    }
    return {};
}

template <typename T>
utility::expected<T, std::error_code> Socket::GetNativeOption(int level, int option) const noexcept {
    T value{};
#if defined(_WIN32)
    int length = static_cast<int>(sizeof(T));
    if (::getsockopt(handle_, level, option, reinterpret_cast<char*>(&value), &length) < 0)
#elif defined(__linux__)
    ::socklen_t length = static_cast< ::socklen_t>(sizeof(T));
    if (::getsockopt(handle_, level, option, &value, &length) < 0)
#endif
    {
        return utility::unexpected<std::error_code>(last_error());
    }
    return value;
}

utility::expected<void, std::error_code> Socket::ToggleReuseAddress(bool toggle) noexcept {
    const int value = toggle ? 1 : 0;
    if (auto error = SetNativeOption(SOL_SOCKET, SO_REUSEADDR, value)) {
        return utility::unexpected<std::error_code>(std::move(error));
    }
    return {};
}

utility::expected<bool, std::error_code> Socket::IsReuseAddress() const noexcept {
    return GetNativeOption<int>(SOL_SOCKET, SO_REUSEADDR).transform([](int value) { return value != 0; });
}

utility::expected<void, std::error_code> Socket::ToggleReusePort(bool toggle) noexcept {
#if defined(SO_REUSEPORT)
    const int value = toggle ? 1 : 0;
    if (auto error = SetNativeOption(SOL_SOCKET, SO_REUSEPORT, value)) {
        return utility::unexpected<std::error_code>(std::move(error));
    }
    return {};
#else
    return utility::unexpected<std::error_code>(std::make_error_code(std::errc::operation_not_supported));
#endif
}

utility::expected<bool, std::error_code> Socket::IsReusePort() const noexcept {
#if defined(SO_REUSEPORT)
    return GetNativeOption<int>(SOL_SOCKET, SO_REUSEPORT).transform([](int value) { return value != 0; });
#else
    return utility::unexpected<std::error_code>(std::make_error_code(std::errc::operation_not_supported));
#endif
}

utility::expected<void, std::error_code> Socket::ToggleKeepAlive(bool enable) noexcept {
    const int value = enable ? 1 : 0;
    if (auto error = SetNativeOption(SOL_SOCKET, SO_KEEPALIVE, value)) {
        return utility::unexpected<std::error_code>(std::move(error));
    }
    return {};
}

utility::expected<bool, std::error_code> Socket::IsKeepAlive() const noexcept {
    return GetNativeOption<int>(SOL_SOCKET, SO_KEEPALIVE).transform([](int value) { return value != 0; });
}

utility::expected<void, std::error_code> Socket::ToggleNoDelay(bool toggle) noexcept {
    const int value = toggle ? 1 : 0;
    if (auto error = SetNativeOption(IPPROTO_TCP, TCP_NODELAY, value)) {
        return utility::unexpected<std::error_code>(std::move(error));
    }
    return {};
}

utility::expected<bool, std::error_code> Socket::IsNoDelay() const noexcept {
    return GetNativeOption<int>(IPPROTO_TCP, TCP_NODELAY).transform([](int value) { return value != 0; });
}

utility::expected<void, std::error_code> Socket::SetLingerDuration(std::chrono::seconds duration) noexcept {
    ::linger value{};
    if (duration.count() < 0 || duration.count() > std::numeric_limits<decltype(value.l_linger)>::max()) {
        return utility::unexpected<std::error_code>(std::make_error_code(std::errc::invalid_argument));
    }
    value.l_onoff = duration.count() != 0;
    value.l_linger = static_cast<decltype(value.l_linger)>(duration.count());
    if (auto error = SetNativeOption(SOL_SOCKET, SO_LINGER, value)) {
        return utility::unexpected<std::error_code>(std::move(error));
    }
    return {};
}

utility::expected<std::chrono::seconds, std::error_code> Socket::GetLingerDuration() const noexcept {
    return GetNativeOption<::linger>(SOL_SOCKET, SO_LINGER).transform([](const ::linger& value) {
        return std::chrono::seconds{value.l_onoff != 0 ? value.l_linger : 0};
    });
}

utility::expected<Address, std::error_code> Socket::GetLocalAddress() const noexcept {
    Address address;
    address.length() = Address::capacity();
    if (::getsockname(handle_, address.storage<sockaddr>(), &address.length()) < 0) {
        return utility::unexpected<std::error_code>(Socket::last_error());
    }
    return address;
}

utility::expected<Address, std::error_code> Socket::GetRemoteAddress() const noexcept {
    Address address;
    address.length() = Address::capacity();
    if (::getpeername(handle_, address.storage<sockaddr>(), &address.length()) < 0) {
        return utility::unexpected<std::error_code>(Socket::last_error());
    }
    return address;
}

void Socket::Shutdown(ShutdownHow how) noexcept {
    if (!is_valid()) {
        return;
    }
    int what = 0;
    switch (how) {
#if defined(__linux__)
        case ShutdownHow::kRead:
            what = SHUT_RD;
            break;
        case ShutdownHow::kWrite:
            what = SHUT_WR;
            break;
        case ShutdownHow::kBoth:
            what = SHUT_RDWR;
            break;
#elif defined(_WIN32)
        case ShutdownHow::kRead:
            what = SD_RECEIVE;
            break;
        case ShutdownHow::kWrite:
            what = SD_Send;
            break;
        case ShutdownHow::kBoth:
            what = SD_BOTH;
            break;
#endif
    }
    ::shutdown(handle_, what);
}

void Socket::Close() noexcept {
    if (!is_valid()) {
        return;
    }
#if defined(__linux__)
    ::close(handle_);
#elif defined(_WIN32)
    ::closesocket(handle_);
#endif
    handle_ = kInvalidHandle;
    ReleaseWinsock();
}
}  // namespace nbio::net
