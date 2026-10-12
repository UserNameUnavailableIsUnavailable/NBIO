#include <gtest/gtest.h>

#include <array>
#include <chrono>
#include <limits>
#include <memory>
#include <nbio/async.hpp>
#include <nbio/core/uring_multiplexer.hpp>
#include <nbio/net/tcp_acceptor.hpp>
#include <nbio/net/tcp_connect_channel.hpp>
#include <nbio/net/tcp_connector.hpp>
#include <nbio/net/socket.hpp>
#include <nbio/runtime/daemon.hpp>
#include <type_traits>
#include <utility>

#if defined(__linux__)
#include <poll.h>
#endif

namespace {
using nbio::net::Address;
using nbio::net::TcpAcceptor;
using nbio::net::TcpConnector;
using nbio::net::Socket;
using namespace std::chrono_literals;

template <typename T>
concept CanConnect = requires(T& socket, const Address& address) { socket.Connect(address); };

template <typename T>
concept CanListen = requires(T& socket) { socket.Listen(); };

static_assert(std::is_base_of_v<Socket, TcpAcceptor>);
static_assert(std::is_base_of_v<Socket, TcpConnector>);
static_assert(!CanConnect<TcpAcceptor>);
static_assert(CanConnect<TcpConnector>);
static_assert(CanListen<TcpAcceptor>);
static_assert(!CanListen<TcpConnector>);

using Toggle = nbio::utility::expected<void, std::error_code> (Socket::*)(bool) noexcept;
using Query = nbio::utility::expected<bool, std::error_code> (Socket::*)() const noexcept;

void CheckOption(Socket& socket, Toggle toggle, Query query) {
    for (bool enabled : {true, false, true, false}) {
        ASSERT_TRUE((socket.*toggle)(enabled));
        const auto result = (std::as_const(socket).*query)();
        ASSERT_TRUE(result);
        EXPECT_EQ(*result, enabled);
    }
}
}  // namespace

TEST(SocketTesting, BooleanOptionsRoundTrip) {
    Socket socket{Address::Family::kIPv4, Socket::Type::kStream};
    CheckOption(socket, &Socket::ToggleReuseAddress, &Socket::IsReuseAddress);
    CheckOption(socket, &Socket::ToggleKeepAlive, &Socket::IsKeepAlive);
    CheckOption(socket, &Socket::ToggleNoDelay, &Socket::IsNoDelay);
#if defined(SO_REUSEPORT)
    CheckOption(socket, &Socket::ToggleReusePort, &Socket::IsReusePort);
#else
    const auto toggle = socket.ToggleReusePort(true);
    ASSERT_FALSE(toggle);
    EXPECT_EQ(toggle.error(), std::errc::operation_not_supported);
    const auto query = socket.IsReusePort();
    ASSERT_FALSE(query);
    EXPECT_EQ(query.error(), std::errc::operation_not_supported);
#endif
}

TEST(SocketTesting, NonBlockingReflectsNativeFlags) {
    Socket socket{Address::Family::kIPv4, Socket::Type::kStream};
#if defined(__linux__)
    const int flags = ::fcntl(socket.native_handle(), F_GETFL);
    ASSERT_GE(flags, 0);
    CheckOption(socket, &Socket::ToggleNonBlocking, &Socket::IsNonBlocking);
    EXPECT_EQ(::fcntl(socket.native_handle(), F_GETFL), flags);
    ASSERT_EQ(::fcntl(socket.native_handle(), F_SETFL, flags | O_NONBLOCK), 0);
    const auto result = socket.IsNonBlocking();
    ASSERT_TRUE(result);
    EXPECT_TRUE(*result);
#elif defined(_WIN32)
    ASSERT_TRUE(socket.ToggleNonBlocking(true));
    ASSERT_TRUE(socket.ToggleNonBlocking(false));
    const auto result = socket.IsNonBlocking();
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error(), std::errc::operation_not_supported);
#endif
}

TEST(SocketTesting, LingerRoundTripAndValidation) {
    Socket socket{Address::Family::kIPv4, Socket::Type::kStream};
    const auto initial = socket.GetLingerDuration();
    ASSERT_TRUE(initial);
    EXPECT_EQ(*initial, 0s);
    for (auto duration : {3s, 7s, 0s}) {
        ASSERT_TRUE(socket.SetLingerDuration(duration));
        const auto result = std::as_const(socket).GetLingerDuration();
        ASSERT_TRUE(result);
        EXPECT_EQ(*result, duration);
    }
    ASSERT_TRUE(socket.SetLingerDuration(3s));
    const auto overflow = std::chrono::seconds{std::numeric_limits<decltype(::linger{}.l_linger)>::max()} + 1s;
    for (auto duration : {-1s, overflow, std::chrono::seconds::max()}) {
        const auto result = socket.SetLingerDuration(duration);
        ASSERT_FALSE(result);
        EXPECT_EQ(result.error(), std::errc::invalid_argument);
        const auto unchanged = socket.GetLingerDuration();
        ASSERT_TRUE(unchanged);
        EXPECT_EQ(*unchanged, 3s);
    }
    ASSERT_TRUE(socket.SetLingerDuration(0s));
    ::linger native{};
#if defined(__linux__)
    ::socklen_t length = sizeof(native);
    ASSERT_EQ(::getsockopt(socket.native_handle(), SOL_SOCKET, SO_LINGER, &native, &length), 0);
#elif defined(_WIN32)
    int length = sizeof(native);
    ASSERT_EQ(::getsockopt(socket.native_handle(), SOL_SOCKET, SO_LINGER, reinterpret_cast<char*>(&native), &length),
              0);
#endif
    EXPECT_EQ(native.l_onoff, 0);
}

TEST(SocketTesting, InvalidSocketOptionsReportErrors) {
    Socket socket;
    EXPECT_FALSE(socket.ToggleNonBlocking(true));
    EXPECT_FALSE(socket.IsNonBlocking());
    EXPECT_FALSE(socket.ToggleReuseAddress(true));
    EXPECT_FALSE(socket.IsReuseAddress());
    EXPECT_FALSE(socket.ToggleReusePort(true));
    EXPECT_FALSE(socket.IsReusePort());
    EXPECT_FALSE(socket.ToggleKeepAlive(true));
    EXPECT_FALSE(socket.IsKeepAlive());
    EXPECT_FALSE(socket.ToggleNoDelay(true));
    EXPECT_FALSE(socket.IsNoDelay());
    EXPECT_FALSE(socket.SetLingerDuration(1s));
    EXPECT_FALSE(socket.GetLingerDuration());
    EXPECT_FALSE(socket.TakeError());
}

TEST(SocketTesting, DerivedSocketsPreserveConnectionAndOwnership) {
    TcpAcceptor original;
    ASSERT_TRUE(original.ToggleReuseAddress(true));
    ASSERT_TRUE(original.Bind(Address::FromIPv4("127.0.0.1", 0).value()));
    const auto handle = original.native_handle();
    TcpAcceptor acceptor{std::move(original)};
    EXPECT_FALSE(original.is_valid());
    EXPECT_EQ(acceptor.native_handle(), handle);
    const auto address = acceptor.GetLocalAddress();
    ASSERT_TRUE(address);

    TcpConnector client;
    ASSERT_TRUE(client.ToggleNoDelay(true));
    ASSERT_TRUE(client.Connect(*address));
    const auto repeated = client.Connect(*address);
    ASSERT_FALSE(repeated);
    EXPECT_EQ(repeated.error(), std::errc::already_connected);
    auto accepted = acceptor.Accept();
    ASSERT_TRUE(accepted);
    EXPECT_TRUE(accepted->is_valid());
    const auto peer = accepted->GetRemoteAddress();
    ASSERT_TRUE(peer);
    EXPECT_EQ(peer->ip(), "127.0.0.1");

    const std::array<char, 1> sent{'x'};
    const auto sent_result = client.Send(sent);
    ASSERT_TRUE(sent_result);
    EXPECT_EQ(*sent_result, sent.size());
    std::array<char, 1> received{};
    const auto received_result = accepted->Receive(received);
    ASSERT_TRUE(received_result);
    EXPECT_EQ(*received_result, received.size());
    EXPECT_EQ(received, sent);

    TcpConnector moved{std::move(*accepted)};
    EXPECT_FALSE(accepted->is_valid());
    EXPECT_TRUE(moved.is_valid());
    const auto client_handle = client.native_handle();
    const auto moved_handle = moved.native_handle();
    swap(client, moved);
    EXPECT_EQ(client.native_handle(), moved_handle);
    EXPECT_EQ(moved.native_handle(), client_handle);
    moved.Shutdown();
    const auto eof = client.Receive(received);
    ASSERT_TRUE(eof);
    EXPECT_EQ(*eof, 0U);
    client.Close();
    EXPECT_FALSE(client.is_valid());
}

#if defined(__linux__)
TEST(SocketTesting, NonBlockingConnectReturnsNativeInProgress) {
    TcpAcceptor acceptor;
    ASSERT_TRUE(acceptor.Bind(Address::FromIPv4("127.0.0.1", 0).value()));
    const auto address = acceptor.GetLocalAddress();
    ASSERT_TRUE(address);
    TcpConnector client;
    ASSERT_TRUE(client.ToggleNonBlocking(true));
    const auto connected = client.Connect(*address);
    ASSERT_FALSE(connected);
    EXPECT_EQ(connected.error(), std::errc::operation_in_progress);
    ::pollfd descriptor{static_cast<int>(client.native_handle()), POLLOUT, 0};
    ASSERT_EQ(::poll(&descriptor, 1, 1000), 1);
    ASSERT_TRUE(client.TakeError());
    EXPECT_TRUE(client.GetRemoteAddress());
}
#endif

namespace {
nbio::async::Task<void> CheckAsyncConnect(const Address& target, bool succeeds) {
    nbio::net::TcpConnectChannel channel{TcpConnector{}, nbio::runtime::Daemon::multiplexer(),
                                         nbio::runtime::Daemon::scheduler()};
    auto connected = co_await channel.Connect(target);
    EXPECT_EQ(connected.has_value(), succeeds);
    if (connected) {
        EXPECT_TRUE(connected->GetRemoteAddress());
        const auto repeated = co_await channel.Connect(target);
        EXPECT_FALSE(repeated);
    } else {
        EXPECT_EQ(connected.error(), std::errc::connection_refused);
    }
}

void CheckAsyncConnections() {
    TcpAcceptor acceptor;
    ASSERT_TRUE(acceptor.Bind(Address::FromIPv4("127.0.0.1", 0).value()));
    const auto address = acceptor.GetLocalAddress();
    ASSERT_TRUE(address);
    nbio::async::Run(CheckAsyncConnect(*address, true));

    Socket reserved{Address::Family::kIPv4, Socket::Type::kStream};
    ASSERT_TRUE(reserved.Bind(Address::FromIPv4("127.0.0.1", 0).value()));
    const auto refused = reserved.GetLocalAddress();
    ASSERT_TRUE(refused);
    nbio::async::Run(CheckAsyncConnect(*refused, false));
}
}  // namespace

TEST(SocketTesting, AsyncConnectPropagatesSuccessAndRefusal) { CheckAsyncConnections(); }

#if defined(NBIO_ENABLE_IO_URING)
TEST(SocketTesting, AsyncConnectPropagatesSuccessAndRefusalOnURing) {
    nbio::runtime::Daemon::Initialize(std::make_unique<nbio::core::URingMultiplexer>());
    CheckAsyncConnections();
}
#endif
