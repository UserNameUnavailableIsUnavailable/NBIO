#pragma once

#include <deque>
#include <nbio/async/coroutine.hpp>
#include <nbio/runtime/daemon.hpp>
#include <nbio/async/scheduler.hpp>
#include <nbio/async/task.hpp>
#include <nbio/core/channel.hpp>
#include <nbio/core/types.hpp>
#include <nbio/net/address.hpp>
#include <nbio/net/payload.hpp>
#include <nbio/net/tcp_listener.hpp>
#include <nbio/net/tcp_connector.hpp>
#include <nbio/net/socket.hpp>
#include <optional>
#include <utility>

namespace nbio::net {
class AcceptAwaiter;

class TcpListenChannel final : public nbio::core::Channel<TcpListenChannel> {
   public:
    using Payload = detail::CommunicationPayload<TcpListenChannel>;

    TcpListenChannel(nbio::net::TcpListener& listener, nbio::core::Multiplexer& multiplexer,
                     nbio::async::Scheduler& scheduler);
    ~TcpListenChannel() noexcept;

    // The next peer that connects, as the connection it arrived on and the address it
    // came from: the socket is accepted on the listener's descriptor by the backend,
    // and the listener is what turns it into a connector -- a connection of the
    // accepted kind can only be made by a listener, because the peer is what makes
    // its data path mean anything.
    nbio::async::Task<utility::expected<std::pair<net::TcpConnector, net::Address>, std::error_code>> Accept();

    // The operation the backend is asked to perform lives in the payload; the
    // backend fills the communication slots and asks the channel to reap them.
    Payload& Submit();
    void Complete();

    net::TcpListener& listener() noexcept { return listener_; }
    const net::TcpListener& listener() const noexcept { return listener_; }

   private:
    friend class AcceptAwaiter;

    // Queues the wait and arms the channel: this is the suspension point, and being
    // armed is what tells the backend to look at the channel.
    void Prepare(async::Coroutine waiter, net::Communication* result);

    nbio::net::TcpListener& listener_;
    // One waiter per communication slot, in queue order.
    std::deque<async::Coroutine> waiters_;
    Payload payload_{};
};
}  // namespace nbio::net
