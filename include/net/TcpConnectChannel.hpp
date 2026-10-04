#pragma once

#include <async/Coroutine.hpp>
#include <async/Scheduler.hpp>
#include <async/Task.hpp>
#include <utility/Expected.hpp>
#include <net/Address.hpp>
#include <net/TcpConnector.hpp>
#include <core/Channel.hpp>
#include <core/Multiplexer.hpp>
#include <net/Payload.hpp>
#include <runtime/Runtime.hpp>
#include <core/Types.hpp>
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

    nbio::async::Task<nbio::runtime, utility::expected<nbio::net::TcpConnector, std::error_code>> Connect(
        const nbio::net::Address& target);

    nbio::async::Task<nbio::runtime, utility::expected<nbio::net::TcpConnector, std::error_code>> Connect(
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




