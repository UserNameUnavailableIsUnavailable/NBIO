#if defined(NBIO_ENABLE_RDMA) && defined(__linux__)
#include <nbio/net/RdmaSendChannel.hpp>

#include <nbio/async/Coroutine.hpp>
#include <cassert>
#include <deque>
#include <utility>

namespace nbio::net {
namespace detail {
// Waits for send completions rather than for one particular send: the caller
// posts several chunks at once, so what it has to know is how many of them the
// device has finished with -- the completions are what hand the chunks back.
class RdmaSendPollAwaiter {
   public:
    RdmaSendPollAwaiter(RdmaSendChannel& channel, std::size_t count) : channel_(channel) {
        const std::size_t outstanding = channel_.outstanding();
        // count == 0 means "all of them", so the target is nothing still in
        // flight when the wait began.
        target_ = count == 0 ? 0 : (outstanding > count ? outstanding - count : 0);
        before_ = channel_.completed();
    }

    bool await_ready() const noexcept {
        if (channel_.outstanding() <= target_) {
            // Nothing to wait for, so nothing to report either: a failure left
            // over from an earlier poll must not be read as this poll's answer.
            channel_.job().error.clear();
            return true;
        }
        return false;
    }

    template <typename PromiseType>
    bool await_suspend(std::coroutine_handle<PromiseType> handle) {
        // Reap what has already completed before Parking. The completion channel
        // is shared with the receive half, so a completion can land without this
        // channel's event ever reaching the multiplexer.
        if (auto reaped = channel_.connection().PollSend(0); reaped) [[likely]]
        {
            channel_.completed() += *reaped;
        } else {
            // The stream will not complete anything again. Answering now, rather
            // than Parking, is what keeps a waiter from being Parked for good.
            channel_.job().error = reaped.error();
            return false;
        }
        if (channel_.outstanding() <= target_) {
            return false;
        }

        channel_.job() = {.target = target_, .completions = 0, .error = {}};
        channel_.Park(nbio::async::Coroutine::FromHandle(handle));
        channel_.Arm();
        return true;
    }

    RdmaResult<std::size_t> await_resume() const {
        auto& job = channel_.job();
        if (job.error) [[unlikely]] {
            // Consumed, so that a later poll on the same channel starts clean.
            return nbio::utility::unexpected(std::exchange(job.error, {}));
        }
        return channel_.completed() - before_;
    }

   private:
    RdmaSendChannel& channel_;
    std::size_t target_{0};
    std::size_t before_{0};
};
}  // namespace detail

RdmaSendChannel::RdmaSendChannel(nbio::net::RdmaConnector& connection, nbio::Core::Multiplexer& multiplexer,
                                 nbio::async::Scheduler& scheduler)
    : nbio::Core::Channel<RdmaSendChannel>(nbio::Core::ChannelType::kRdmaSend,
                                                 static_cast<std::uintptr_t>(connection.completion_channel_handle()), multiplexer,
                                                 scheduler),
      connection_(connection) {
    // Registered on the first arm(): nothing to watch until a poll is Parked.
}

RdmaSendChannel::~RdmaSendChannel() noexcept { multiplexer_.DeleteChannel(this); }

nbio::async::Task<RdmaResult<std::size_t>> RdmaSendChannel::Poll(std::size_t count) {
    co_return co_await detail::RdmaSendPollAwaiter{*this, count};
}

RdmaSendChannel::Payload& RdmaSendChannel::Submit() { return payload_; }

void RdmaSendChannel::Complete() {
    auto& payload = payload_;
    payload.ReleasePoll();

    if (!waiter_) [[unlikely]] {
        // Nothing is waiting, so nothing may be reaped: a completion taken here
        // would have no poll to belong to.
        return;
    }

    if (auto reaped = connection_.PollSend(0); reaped) [[likely]]
    {
        completed_ += *reaped;
        if (connection_.outstanding_sends() > job().target && !connection_.is_peer_closed() && !connection_.failed()) {
            Arm();
            return;
        }
    } else {
        job().error = reaped.error();
    }

    auto waiter = std::exchange(waiter_, {});
    scheduler_.Submit(std::move(waiter));
}
}  // namespace nbio::net

#endif  // defined(NBIO_ENABLE_RDMA) && defined(__linux__)
