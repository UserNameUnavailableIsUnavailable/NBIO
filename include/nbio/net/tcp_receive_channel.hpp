#pragma once

#include <deque>
#include <nbio/runtime/daemon.hpp>
#include <nbio/async/scheduler.hpp>
#include <nbio/async/task.hpp>
#include <nbio/core/channel.hpp>
#include <nbio/core/types.hpp>
#include <nbio/net/payload.hpp>
#include <nbio/net/tcp_connector.hpp>
#include <nbio/utility/buffer.hpp>
#include <span>
#include <system_error>

namespace nbio::net {
class ReceiveAwaiter;

class TcpReceiveChannel final : public nbio::core::Channel<TcpReceiveChannel> {
   public:
    using Payload = detail::MessagePayload<TcpReceiveChannel>;

    explicit TcpReceiveChannel(nbio::net::TcpConnector& connector, nbio::core::Multiplexer& multiplexer,
                               nbio::async::Scheduler& scheduler);
    ~TcpReceiveChannel() noexcept;

    nbio::async::Task<utility::expected<std::size_t, std::error_code>> Receive(std::span<char> buffer);

    // The operation the backend performs lives in the payload; the backend fills
    // the transmissions and asks the channel to reap them.
    Payload& Submit();
    void Complete();

    net::TcpConnector& connector() noexcept { return connector_; }
    const net::TcpConnector& connector() const noexcept { return connector_; }

   private:
    friend class ReceiveAwaiter;

    // Queues the receive and arms the channel: this is the suspension point, and
    // being armed is what tells the backend to look at the channel.
    void Prepare(async::Coroutine waiter, net::Transmission* transmission);

    nbio::net::TcpConnector& connector_;
    // One waiter per transmission, in queue order.
    std::deque<async::Coroutine> waiters_;
    Payload payload_{};
};
}  // namespace nbio::net
