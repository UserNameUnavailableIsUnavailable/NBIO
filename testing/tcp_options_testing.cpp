#include <gtest/gtest.h>

#include <nbio/net/tcp_listener.hpp>
#include <nbio/net/tcp_connector.hpp>
#include <nbio/net/socket.hpp>
#include <netinet/tcp.h>
#include <sys/socket.h>

namespace {
template <typename SocketType>
void ExpectNoDelay(const SocketType& socket, int expected) {
    int value = -1;
    socklen_t size = sizeof(value);
    ASSERT_EQ(::getsockopt(static_cast<int>(socket.native_handle()), IPPROTO_TCP, TCP_NODELAY, &value, &size), 0);
    EXPECT_EQ(value, expected);
}

template <typename SocketType>
void CheckToggle(SocketType& socket) {
    ExpectNoDelay(socket, 0);
    ASSERT_TRUE(socket.ToggleNoDelay(true));
    ExpectNoDelay(socket, 1);
    ASSERT_TRUE(socket.ToggleNoDelay(false));
    ExpectNoDelay(socket, 0);
}
}  // namespace

TEST(TcpOptionsTesting, SocketCanToggleNoDelay) {
    nbio::net::Socket socket(nbio::net::Address::Family::kIPv4, nbio::net::Socket::Type::kStream);
    CheckToggle(socket);
    socket.Close();
    EXPECT_FALSE(socket.ToggleNoDelay(true));
}

TEST(TcpOptionsTesting, ConnectorCanToggleNoDelay) {
    nbio::net::TcpConnector connector;
    CheckToggle(connector);
    connector.Close();
    EXPECT_FALSE(connector.ToggleNoDelay(true));
}

TEST(TcpOptionsTesting, ListenerCanToggleNoDelay) {
    nbio::net::TcpListener listener;
    CheckToggle(listener);
    listener.Close();
    EXPECT_FALSE(listener.ToggleNoDelay(true));
}

TEST(TcpOptionsTesting, AcceptedConnectionsInheritNoDelay) {
    nbio::net::TcpListener listener;
    ASSERT_TRUE(listener.Bind(nbio::net::Address::FromV4("127.0.0.1", 0)));
    sockaddr_in local{};
    socklen_t size = sizeof(local);
    ASSERT_EQ(::getsockname(static_cast<int>(listener.native_handle()), reinterpret_cast<sockaddr*>(&local), &size),
              0);
    const auto address = nbio::net::Address::FromV4("127.0.0.1", ntohs(local.sin_port));
    for (const bool enabled : {true, false}) {
        ASSERT_TRUE(listener.ToggleNoDelay(enabled));
        nbio::net::TcpConnector client;
        ASSERT_TRUE(client.Connect(address));
        auto accepted = listener.Accept();
        ASSERT_TRUE(accepted);
        ExpectNoDelay(*accepted, enabled ? 1 : 0);
        ASSERT_TRUE(accepted->ToggleNoDelay(!enabled));
        ExpectNoDelay(*accepted, enabled ? 0 : 1);
    }
}
