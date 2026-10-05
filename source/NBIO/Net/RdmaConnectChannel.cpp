#include <NBIO/Net/RdmaConnectChannel.hpp>

#include <NBIO/Async/Coroutine.hpp>
#include <cstdint>
#include <utility>

namespace NBIO::Net {
namespace detail {
class RdmaConnectAwaiter {
   public:
    RdmaConnectAwaiter(RdmaConnectChannel& channel, NBIO::Net::Address peer)
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
            return NBIO::Utility::unexpected(std::exchange(job.error, {}));
        }
        return {};
    }

   private:
    RdmaConnectChannel& channel_;
    NBIO::Net::Address peer_;
};
}  // namespace detail

RdmaConnectChannel::RdmaConnectChannel(NBIO::Net::RdmaConnector& connector, NBIO::Core::Multiplexer& multiplexer,
                                       NBIO::Async::Scheduler& scheduler)
    : NBIO::Core::Channel<RdmaConnectChannel>(NBIO::Core::ChannelType::kRdmaConnect,
                                                    static_cast<std::uintptr_t>(connector.event_channel_handle()), multiplexer,
                                                    scheduler),
      connector_(connector) {
    // What there is to watch is the CM channel the connection was created with: the
    // handshake it is about to run reports there. The connect itself is made
    // synchronously in the awaiter, which never suspends.
}

RdmaConnectChannel::~RdmaConnectChannel() noexcept { multiplexer_.DeleteChannel(this); }

NBIO::Async::Task<RdmaResult<void>> RdmaConnectChannel::Connect(NBIO::Net::Address peer) {
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
}  // namespace NBIO::Net
