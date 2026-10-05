#pragma once

#include <nbio/async/coroutine.hpp>
#include <nbio/async/scheduler.hpp>
#include <nbio/async/task.hpp>
#include <nbio/utility/expected.hpp>
#include <nbio/net/address.hpp>
#include <nbio/net/tcp_connector.hpp>
#include <nbio/core/channel.hpp>
#include <nbio/core/types.hpp>
#include <nbio/net/payload.hpp>
#include <nbio/async/runtime.hpp>
#include <nbio/core/types.hpp>
#include <system_error>
#include <utility>

namespace nbio::net {
class ConnectAwaiter;
class TcpConnectChannel final : public nbio::core::Channel<TcpConnectChannel> {
   public:
    using Payload = detail::PollPayload<TcpConnectChannel>;

    TcpConnectChannel(nbio::net::TcpConnector connector, nbio::core::Multiplexer& multiplexer,
                      nbio::async::Scheduler& scheduler);

    ~TcpConnectChannel() noexcept;

    nbio::async::Task<utility::expected<nbio::net::TcpConnector, std::error_code>> Connect(
        const nbio::net::Address& target);

    nbio::async::Task<utility::expected<nbio::net::TcpConnector, std::error_code>> Connect(
        const nbio::net::Address& source, const nbio::net::Address& target);

    Payload& Submit();
    void Complete();

   private:
    friend class ConnectAwaiter;

    void Park(async::Coroutine waiter) noexcept { waiter_ = std::move(waiter); }

    nbio::net::TcpConnector connector_;
    async::Coroutine waiter_{};
    Payload payload_{};
};
}  // namespace nbio::net




