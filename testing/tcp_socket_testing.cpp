#include <gtest/gtest.h>

#include <array>
#include <chrono>
#include <limits>
#include <nbio/net/tcp_acceptor.hpp>
#include <nbio/net/tcp_connector.hpp>
#include <nbio/net/tcp_socket.hpp>
#include <type_traits>
#include <utility>

namespace {
using nbio::net::Address;
using nbio::net::TcpAcceptor;
using nbio::net::TcpConnector;
using nbio::net::TcpSocket;
using namespace std::chrono_literals;

template <typename T>
concept CanConnect = requires(T& socket, const Address& address) { socket.Connect(address); };

template <typename T>
concept CanListen = requires(T& socket) { socket.Listen(); };

static_assert(std::is_base_of_v<TcpSocket, TcpAcceptor>);
static_assert(std::is_base_of_v<TcpSocket, TcpConnector>);
static_assert(!CanConnect<TcpAcceptor>);
static_assert(CanConnect<TcpConnector>);
static_assert(CanListen<TcpAcceptor>);
static_assert(!CanListen<TcpConnector>);

using Toggle = nbio::utility::expected<void, std::error_code> (TcpSocket::*)(bool) noexcept;
using Query = nbio::utility::expected<bool, std::error_code> (TcpSocket::*)() const noexcept;

void CheckOption(TcpSocket& socket, Toggle toggle, Query query) {
    for (bool enabled : {true, false, true, false}) {
        ASSERT_TRUE((socket.*toggle)(enabled));
        const auto result = (std::as_const(socket).*query)();
        ASSERT_TRUE(result);
        EXPECT_EQ(*result, enabled);
    }
}
}  // namespace

TEST(TcpSocketTesting, BooleanOptionsRoundTrip) {
    TcpSocket socket{Address::Family::kIPv4, TcpSocket::Type::kStream};
    CheckOption(socket, &TcpSocket::ToggleReuseAddress, &TcpSocket::IsReuseAddress);
    CheckOption(socket, &TcpSocket::ToggleKeepAlive, &TcpSocket::IsKeepAlive);
    CheckOption(socket, &TcpSocket::ToggleNoDelay, &TcpSocket::IsNoDelay);
#if defined(SO_REUSEPORT)
    CheckOption(socket, &TcpSocket::ToggleReusePort, &TcpSocket::IsReusePort);
#else
    const auto toggle = socket.ToggleReusePort(true);
    ASSERT_FALSE(toggle);
    EXPECT_EQ(toggle.error(), std::errc::operation_not_supported);
    const auto query = socket.IsReusePort();
    ASSERT_FALSE(query);
    EXPECT_EQ(query.error(), std::errc::operation_not_supported);
#endif
}

TEST(TcpSocketTesting, NonBlockingReflectsNativeFlags) {
    TcpSocket socket{Address::Family::kIPv4, TcpSocket::Type::kStream};
#if defined(__linux__)
    const int flags = ::fcntl(socket.native_handle(), F_GETFL);
    ASSERT_GE(flags, 0);
    CheckOption(socket, &TcpSocket::ToggleNonBlocking, &TcpSocket::IsNonBlocking);
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

TEST(TcpSocketTesting, LingerRoundTripAndValidation) {
    TcpSocket socket{Address::Family::kIPv4, TcpSocket::Type::kStream};
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

TEST(TcpSocketTesting, InvalidSocketOptionsReportErrors) {
    TcpSocket socket;
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

TEST(TcpSocketTesting, DerivedSocketsPreserveConnectionAndOwnership) {
    TcpAcceptor original;
    ASSERT_TRUE(original.ToggleReuseAddress(true));
    ASSERT_TRUE(original.Bind(Address::FromV4("127.0.0.1", 0)));
    const auto handle = original.native_handle();
    TcpAcceptor acceptor{std::move(original)};
    EXPECT_FALSE(original.is_valid());
    EXPECT_EQ(acceptor.native_handle(), handle);
    const auto address = acceptor.GetLocalAddress();
    ASSERT_TRUE(address);

    TcpConnector client;
    ASSERT_TRUE(client.ToggleNoDelay(true));
    ASSERT_TRUE(client.Connect(*address));
    auto accepted = acceptor.Accept();
    ASSERT_TRUE(accepted);
    EXPECT_TRUE(accepted->is_valid());
    const auto peer = accepted->GetPeerAddress();
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
    ASSERT_TRUE(moved.Shutdown());
    const auto eof = client.Receive(received);
    ASSERT_TRUE(eof);
    EXPECT_EQ(*eof, 0U);
    client.Close();
    EXPECT_FALSE(client.is_valid());
}
