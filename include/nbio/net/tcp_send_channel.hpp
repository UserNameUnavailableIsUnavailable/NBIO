#pragma once

#include <nbio/async/scheduler.hpp>
#include <nbio/async/task.hpp>
#include <nbio/utility/buffer.hpp>
#include <nbio/net/tcp_connector.hpp>
#include <deque>
#include <span>
#include <system_error>

#include <nbio/core/channel.hpp>
#include <nbio/core/types.hpp>
#include <nbio/net/payload.hpp>
#include <nbio/async/runtime.hpp>

namespace nbio::net {
class SendAwaiter;

class TcpSendChannel final : public nbio::core::Channel<TcpSendChannel> {
   public:
    using Payload = detail::MessagePayload<TcpSendChannel>;

    explicit TcpSendChannel(nbio::net::TcpConnector& connector, nbio::core::Multiplexer& multiplexer,
                            nbio::async::Scheduler& scheduler);
    ~TcpSendChannel() noexcept;

    nbio::async::Task<utility::expected<std::size_t, std::error_code>> Send(std::span<const char> buffer);

    // The operation the backend performs lives in the payload; the backend fills
    // the transmissions and asks the channel to reap them.
    Payload& Submit();
    void Complete();

    net::TcpConnector& connector() noexcept { return connector_; }

    const net::TcpConnector& connector() const noexcept { return connector_; }

   private:
    friend class SendAwaiter;

    void Prepare(nbio::async::Coroutine waiter, net::Transmission* transmission);

    nbio::net::TcpConnector& connector_;
    // One waiter per transmission, in queue order.
    std::deque<async::Coroutine> waiters_;
    Payload payload_{};
};
}  // namespace nbio::net




