#if defined(NBIO_ENABLE_RDMA) && defined(__linux__)

#include <nbio/net/rdma_receive_channel.hpp>
#include <nbio/async/coroutine.hpp>
#include <cassert>
#include <cstdint>
#include <utility>

namespace nbio::net {
namespace {
// Reaps the completion queue and answers the next received chunk. The
// completion channel is shared with the send half, so a completion can land
// without this channel's event ever reaching the multiplexer: the send half's
// poll drains the shared channel, and the entry is then sitting in the receive
// queue with nothing left to wake a Parked receive for it. Reaping first, every
// time, is what keeps that from becoming a stall.
RdmaResult<RdmaReceiveResult> Reap(RdmaReceiveChannel& channel) {
    if (auto reaped = channel.connection().PollReceive(0); !reaped) [[unlikely]]
    {
        return nbio::utility::unexpected(reaped.error());
    }
    return channel.connection().Receive();
}
}  // namespace

namespace detail {
class RdmaReceiveAwaiter {
   public:
    RdmaReceiveAwaiter(RdmaReceiveChannel& channel) : channel_(channel) {}

    bool await_ready() const noexcept { return false; }

    template <typename PromiseType>
    bool await_suspend(std::coroutine_handle<PromiseType> handle) {
        auto chunk = Reap(channel_);
        if (!chunk) {
            // Nothing will arrive on a stream that has failed, so Parking would
            // leave the waiter there for good.
            channel_.job().error = chunk.error();
            return false;
        }
        if (chunk->state != RdmaReceiveState::kWouldBlock) {
            channel_.job().result = std::move(*chunk);
            channel_.job().error.clear();
            return false;
        }

        channel_.job().result = *chunk;
        channel_.job().error.clear();
        channel_.Park(nbio::async::Coroutine::FromHandle(handle));
        channel_.Arm();
        return true;
    }

    RdmaResult<RdmaReceiveResult> await_resume() const {
        auto& job = channel_.job();
        if (job.error) [[unlikely]] {
            // Consumed, so a later receive on this channel starts clean.
            return nbio::utility::unexpected(std::exchange(job.error, std::error_code{}));
        }
        return job.result;
    }

   private:
    RdmaReceiveChannel& channel_;
};

// The same, but for a caller that is willing to carry on without one. It never
// Parks, so it must not be used while another coroutine is waiting on the same
// channel: there is one job per channel, and this one would take it.
class RdmaReceiveTryAwaiter {
   public:
    explicit RdmaReceiveTryAwaiter(RdmaReceiveChannel& channel) : channel_(channel) {}

    bool await_ready() const noexcept { return false; }

    template <typename PromiseType>
    bool await_suspend(std::coroutine_handle<PromiseType>) noexcept {
        // Still has to reap: a completion that is already in the queue would
        // otherwise sit there with nobody left to deliver it, because nobody is
        // Parking to be woken for it.
        auto chunk = Reap(channel_);
        if (!chunk) [[unlikely]] {
            channel_.job().error = chunk.error();
            return false;
        }
        channel_.job().error.clear();
        channel_.job().result = std::move(*chunk);
        return false;
    }

    RdmaResult<RdmaReceiveResult> await_resume() const {
        auto& job = channel_.job();
        if (job.error) [[unlikely]] {
            return nbio::utility::unexpected(std::exchange(job.error, std::error_code{}));
        }
        return job.result;
    }

   private:
    RdmaReceiveChannel& channel_;
};
}  // namespace detail

RdmaReceiveChannel::RdmaReceiveChannel(nbio::net::RdmaConnector& connection, nbio::core::Multiplexer& multiplexer,
                                       nbio::async::Scheduler& scheduler)
    : nbio::core::Channel<RdmaReceiveChannel>(nbio::core::ChannelType::kRdmaReceive,
                                                    static_cast<std::uintptr_t>(connection.completion_channel_handle()),
                                                    multiplexer, scheduler),
      connection_(connection) {
    // Registered on the first arm(): nothing to watch until a receive is Parked.
}

RdmaReceiveChannel::~RdmaReceiveChannel() noexcept { multiplexer_.DeleteChannel(this); }

nbio::async::Task<RdmaResult<RdmaReceiveResult>> RdmaReceiveChannel::Receive() {
    co_return co_await detail::RdmaReceiveAwaiter{*this};
}

nbio::async::Task<RdmaResult<RdmaReceiveResult>> RdmaReceiveChannel::TryReceive() {
    co_return co_await detail::RdmaReceiveTryAwaiter{*this};
}

RdmaResult<void> RdmaReceiveChannel::Release(std::span<char> chunk) noexcept {
    return connection_.Release(chunk);
}

RdmaReceiveChannel::Payload& RdmaReceiveChannel::Submit() { return payload_; }

void RdmaReceiveChannel::Complete() {
    auto& payload = payload_;
    payload.ReleasePoll();

    if (!waiter_) [[unlikely]] {
        return;
    }

    auto received = Reap(*this);
    if (!received) [[unlikely]] {
        job().error = received.error();
    } else if (received->state == RdmaReceiveState::kWouldBlock) {
        // The completion belonged to the send half; keep waiting for a receive.
        Arm();
        return;
    } else {
        job().result = std::move(*received);
    }

    auto waiter = std::exchange(waiter_, {});
    scheduler_.Submit(std::move(waiter));
}
}  // namespace nbio::net
#endif  // defined(NBIO_ENABLE_RDMA) && defined(__linux__)