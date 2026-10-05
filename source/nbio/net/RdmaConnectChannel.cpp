#include <nbio/net/RdmaConnectChannel.hpp>

#include <nbio/async/Coroutine.hpp>
#include <cstdint>
#include <utility>

namespace nbio::net {
namespace detail {
class RdmaConnectAwaiter {
   public:
    RdmaConnectAwaiter(RdmaConnectChannel& channel, nbio::net::Address peer)
        : channel_(channel), peer_(std::move(peer)) {}

    bool await_ready() const noexcept { return false; }

    template <typename PromiseType>
    bool await_suspend(std::coroutine_handle<PromiseType>) {
        channel_.job().error.clear();

        if (auto connected = channel_.connector().Connect(peer_); connected) {
        } else {
            channel_.job().error = connected.error();
        }
        return false;
    }

    RdmaResult<void> await_resume() {
        auto& job = channel_.job();
        if (job.error) {
            return nbio::utility::unexpected(std::exchange(job.error, {}));
        }
        return {};
    }

   private:
    RdmaConnectChannel& channel_;
    nbio::net::Address peer_;
};
}  // namespace detail

RdmaConnectChannel::RdmaConnectChannel(nbio::net::RdmaConnector& connector, nbio::Core::Multiplexer& multiplexer,
                                       nbio::async::Scheduler& scheduler)
    : nbio::Core::Channel<RdmaConnectChannel>(nbio::Core::ChannelType::kRdmaConnect,
                                                    static_cast<std::uintptr_t>(connector.event_channel_handle()), multiplexer,
                                                    scheduler),
      connector_(connector) {
    // What there is to watch is the CM channel the connection was created with: the
    // handshake it is about to run reports there. The connect itself is made
    // synchronously in the awaiter, which never suspends.
}

RdmaConnectChannel::~RdmaConnectChannel() noexcept { multiplexer_.DeleteChannel(this); }

nbio::async::Task<RdmaResult<void>> RdmaConnectChannel::Connect(nbio::net::Address peer) {
    co_return co_await detail::RdmaConnectAwaiter{*this, std::move(peer)};
}

RdmaConnectChannel::Payload& RdmaConnectChannel::Submit() { return payload_; }

void RdmaConnectChannel::Complete() {
    auto& payload = payload_;
    payload.ReleasePoll();

    if (!waiter_) [[unlikely]] {
        return;
    }

    auto waiter = std::exchange(waiter_, {});
    scheduler_.Submit(std::move(waiter));
}
}  // namespace nbio::net
