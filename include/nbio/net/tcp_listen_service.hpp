#pragma once

#include <cstdint>
#include <memory>
#include <nbio/runtime/daemon.hpp>
#include <nbio/async/task.hpp>
#include <nbio/net/address.hpp>
#include <nbio/net/tcp_listen_channel.hpp>
#include <nbio/net/tcp_listener.hpp>
#include <nbio/net/tcp_session_service.hpp>
#include <nbio/utility/expected.hpp>
#include <system_error>
#include <utility>

namespace nbio::net {
// A listener with the accept channel that waits on it: the server side's way in,
// attached to the engine installed on this thread.
//
// What it answers is the whole of a connection -- the session that will carry it and
// the address it came from -- because accepting is the one way to hold a connection
// whose far end nobody chose, so that address is the one thing about it that cannot be
// looked up on the other side.
//
// The channel is built on the listener and keeps a reference to it, which is why the
// listener is declared first here and why both outlive whatever is served.
class TcpListenService final {
   public:
    // Listens on `address`. The listener is bound here, before anything can wait on
    // it, so a service that exists is a service that can accept -- and a bind the
    // kernel refuses is a failure to start rather than one to discover later.
    TcpListenService(const nbio::net::Address& address, int backlog = 4096);

    TcpListenService(const TcpListenService&) = delete;
    TcpListenService& operator=(const TcpListenService&) = delete;
    TcpListenService(TcpListenService&&) = delete;
    TcpListenService& operator=(TcpListenService&&) = delete;

    ~TcpListenService() noexcept;

    nbio::async::Task<
        utility::expected<std::pair<std::shared_ptr<TcpSessionService>, nbio::net::Address>, std::error_code>>
    Accept();

    nbio::net::TcpListener& listener() noexcept { return listener_; }

    const nbio::net::TcpListener& listener() const noexcept { return listener_; }

   private:
    nbio::net::TcpListener listener_;
    TcpListenChannel channel_;
};
}  // namespace nbio::net
