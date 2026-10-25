#if defined(NBIO_ENABLE_RDMA) && defined(__linux__)

#include <cassert>
#include <nbio/async/coroutine.hpp>
#include <nbio/net/rdma_send_channel.hpp>
#include <utility>

namespace nbio::net {
namespace detail {
/**
 * @brief The coroutine side of @ref RdmaSendChannel::Wait.
 *
 * @details Asks before it parks: a completion that has already been reaped must
 * not suspend at all, and a sequence number that cannot become ready must answer
 * at once rather than leave the caller parked for something that will never come.
 */
class RdmaSendWaitAwaiter {
   public:
    RdmaSendWaitAwaiter(RdmaSendChannel& channel, RdmaSeq seq) : channel_(channel), seq_(seq) {}

    bool await_ready() const noexcept { return false; }

    template <typename PromiseType>
    bool await_suspend(std::coroutine_handle<PromiseType> handle) {
        auto& job = channel_.job();
        job = RdmaSendChannel::Pending{.seq = seq_};

        if (auto fetched = channel_.connection().FetchSend(seq_); fetched) [[likely]] {
            job.result = *fetched;
            return false;
        } else if (fetched.error() != make_error_code(RdmaErrc::kNotReady)) {
            job.error = fetched.error();
            return false;
        }

        // One non-blocking reap, then ask again: the completion may have arrived
        // without anyone having polled the fd yet.
        if (auto waited = channel_.connection().WaitSend(seq_, 0); !waited) [[unlikely]] {
            job.error = waited.error();
            return false;
        }
        if (auto fetched = channel_.connection().FetchSend(seq_); fetched) [[likely]] {
            job.result = *fetched;
            return false;
        }

        channel_.Park(nbio::async::Coroutine::FromHandle(handle));
        channel_.Arm();
        return true;
    }

    RdmaResult<RdmaSendResult> await_resume() const {
        auto& job = channel_.job();
        if (job.error) [[unlikely]] {
            // Consumed, so that a later pull on the same channel starts clean.
            return nbio::utility::unexpected(std::exchange(job.error, {}));
        }
        // A wait consumes: the result is handed out once, and the slot is released
        // so that the ring can advance.
        channel_.connection().DropSend(job.result.seq);
        return job.result;
    }

   private:
    RdmaSendChannel& channel_;
    RdmaSeq seq_;
};
}  // namespace detail

RdmaSendChannel::RdmaSendChannel(nbio::net::RdmaConnector& connection, nbio::core::Multiplexer& multiplexer,
                                 nbio::async::Scheduler& scheduler)
    : nbio::core::Channel<RdmaSendChannel>(nbio::core::ChannelType::kRdmaSend,
                                           static_cast<std::uintptr_t>(connection.completion_channel_handle()),
                                           multiplexer, scheduler),
      connection_(connection) {
    // Registered on the first arm(): nothing to watch until a pull is parked.
}

RdmaSendChannel::~RdmaSendChannel() noexcept { multiplexer_.DeleteChannel(this); }

RdmaResult<RdmaSeq> RdmaSendChannel::Post(std::span<const char> buffer) noexcept { return connection_.PostSend(buffer); }

nbio::async::Task<RdmaResult<RdmaSendResult>> RdmaSendChannel::Wait(RdmaSeq seq) {
    co_return co_await detail::RdmaSendWaitAwaiter{*this, seq};
}

RdmaSendChannel::Payload& RdmaSendChannel::Submit() { return payload_; }

void RdmaSendChannel::Complete() {
    auto& payload = payload_;
    payload.ReleasePoll();

    if (!waiter_) [[unlikely]] {
        // Nothing is waiting, so nothing may be reaped on its behalf: a completion
        // taken here would have no pull to belong to.
        return;
    }

    auto& job = job_;
    auto waited = connection_.WaitSend(job.seq, 0);
    if (!waited) [[unlikely]] {
        job.error = waited.error();
    } else if (*waited >= job.seq) {
        if (auto fetched = connection_.FetchSend(job.seq); fetched) [[likely]] {
            job.result = *fetched;
        } else {
            job.error = fetched.error();
        }
    } else {
        // The event was not for this sequence number after all: arm again and keep
        // the puller parked.
        Arm();
        return;
    }

    auto waiter = std::exchange(waiter_, {});
    scheduler_.Submit(std::move(waiter));
}
}  // namespace nbio::net

#endif  // defined(NBIO_ENABLE_RDMA) && defined(__linux__)
