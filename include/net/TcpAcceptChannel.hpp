#pragma once

#include <async/Coroutine.hpp>
#include <async/Scheduler.hpp>
#include <async/Task.hpp>
#include <net/Address.hpp>
#include <net/TcpAcceptor.hpp>
#include <net/TcpConnector.hpp>
#include <net/TcpSocket.hpp>
#include <core/Channel.hpp>
#include <core/Multiplexer.hpp>
#include <net/Payload.hpp>
#include <runtime/Runtime.hpp>
#include <core/Types.hpp>
#include <deque>
#include <optional>
#include <utility>

namespace nbio::net {
class AcceptAwaiter;

class TcpAcceptChannel final : public nbio::core::Channel<TcpAcceptChannel> {
   public:
    using Payload = detail::CommunicationPayload<TcpAcceptChannel>;

    TcpAcceptChannel(nbio::net::TcpAcceptor& acceptor, nbio::core::Multiplexer& multiplexer,
                     nbio::async::Scheduler& scheduler);
    ~TcpAcceptChannel() noexcept;

    // The next peer that connects, as the connection it arrived on and the address it
    // came from: the socket is accepted on the listener's descriptor by the backend,
    // and the listener is what turns it into a connector -- a connection of the
    // accepted kind can only be made by a listener, because the peer is what makes
    // its data path mean anything.
    nbio::async::Task<nbio::runtime, utility::expected<std::pair<net::TcpConnector, net::Address>, std::error_code>>
    Accept();

    // The operation the backend is asked to perform lives in the payload; the
    // backend fills the communication slots and asks the channel to reap them.
    Payload& Submit();
    void Complete();

    net::TcpAcceptor& acceptor() noexcept { return acceptor_; }
    const net::TcpAcceptor& acceptor() const noexcept { return acceptor_; }

   private:
    friend class AcceptAwaiter;

    // Queues the wait and arms the channel: this is the suspension point, and being
    // armed is what tells the backend to look at the channel.
    void Prepare(async::Coroutine waiter, net::Communication* result);

    nbio::net::TcpAcceptor& acceptor_;
    // One waiter per communication slot, in queue order.
    std::deque<async::Coroutine> waiters_;
    Payload payload_{};
};
}  // namespace nbio::net




