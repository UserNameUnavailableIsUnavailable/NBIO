#include <cstdint>
#include <nbio/async/coroutine.hpp>
#include <nbio/net/rdma_connect_channel.hpp>
#include <utility>

namespace nbio::net {
namespace detail {
/**
 * @brief The coroutine side of @ref RdmaConnectChannel::Establish.
 *
 * @details One non-blocking step first, so an already-established connection does
 * not suspend at all; then the coroutine parks and the multiplexer wakes it when
 * the event fd says the handshake has moved.
 */
class RdmaEstablishAwaiter {
   public:
    explicit RdmaEstablishAwaiter(RdmaConnectChannel& channel) : channel_(channel) {}

    bool await_ready() const noexcept { return channel_.connector().is_established(); }

    template <typename PromiseType>
    bool await_suspend(std::coroutine_handle<PromiseType> handle) {
        auto& job = channel_.job();
        job.error.clear();

        auto step = channel_.Advance();
        if (step) [[likely]] {
            return false;
        }
        if (step.error() != make_error_code(RdmaErrc::kNotReady)) [[unlikely]] {
            job.error = step.error();
            return false;
        }

        channel_.Park(nbio::async::Coroutine::FromHandle(handle));
        channel_.Arm();
        return true;
    }

    RdmaResult<void> await_resume() const {
        auto& job = channel_.job();
        if (job.error) [[unlikely]] {
            return nbio::utility::unexpected(std::exchange(job.error, {}));
        }
        return {};
    }

   private:
    RdmaConnectChannel& channel_;
};
}  // namespace detail
RdmaConnectChannel::RdmaConnectChannel(nbio::net::RdmaConnector& connector, nbio::core::Multiplexer& multiplexer,
                                       nbio::async::Scheduler& scheduler)
    : nbio::core::Channel<RdmaConnectChannel>(nbio::core::ChannelType::kRdmaConnect,
                                              static_cast<std::uintptr_t>(connector.event_channel_handle()),
                                              multiplexer, scheduler),
      connector_(connector) {
    // What there is to watch is the CM channel the connection was created with: the
    // handshake it is about to run reports there. No wait is made here; Establish
    // parks on this fd and takes one step per wake.
}

RdmaConnectChannel::~RdmaConnectChannel() noexcept { multiplexer_.DeleteChannel(this); }

RdmaResult<void> RdmaConnectChannel::BindSource(const nbio::net::Address& local) noexcept {
    return connector_.BindSource(local);
}

RdmaResult<void> RdmaConnectChannel::BindTarget(const nbio::net::Address& peer) noexcept {
    return connector_.BindTarget(peer);
}

nbio::async::Task<RdmaResult<void>> RdmaConnectChannel::Establish() {
    co_return co_await detail::RdmaEstablishAwaiter{*this};
}

RdmaResult<void> RdmaConnectChannel::Advance() noexcept { return connector_.AdvanceEstablish(); }

RdmaConnectChannel::Payload& RdmaConnectChannel::Submit() { return payload_; }

void RdmaConnectChannel::Complete() {
    auto& payload = payload_;
    payload.ReleasePoll();

    if (!waiter_) [[unlikely]] {
        return;
    }

    auto step = connector_.AdvanceEstablish();
    if (step) {
        job().error.clear();
    } else if (step.error() == make_error_code(RdmaErrc::kNotReady)) {
        // The event was about the connection but not its outcome: arm again and
        // keep the waiter parked.
        Arm();
        return;
    } else {
        job().error = step.error();
    }

    auto waiter = std::exchange(waiter_, {});
    scheduler_.Submit(std::move(waiter));
}
}  // namespace nbio::net
