#if defined(NBIO_ENABLE_RDMA) && defined(__linux__)

#include <cassert>
#include <nbio/async/coroutine.hpp>
#include <nbio/net/rdma_receive_channel.hpp>
#include <utility>

namespace nbio::net {
namespace detail {
/**
 * @brief The coroutine side of @ref RdmaReceiveChannel::Wait.
 *
 * @details The mirror of @c RdmaSendWaitAwaiter: ask, reap once, ask again, and
 * only then park.
 */
class RdmaReceiveWaitAwaiter {
   public:
    RdmaReceiveWaitAwaiter(RdmaReceiveChannel& channel, RdmaSeq seq) : channel_(channel), seq_(seq) {}

    bool await_ready() const noexcept { return false; }

    template <typename PromiseType>
    bool await_suspend(std::coroutine_handle<PromiseType> handle) {
        auto& job = channel_.job();
        job = RdmaReceiveChannel::Pending{.seq = seq_};

        if (auto fetched = channel_.connection().FetchReceive(seq_); fetched) [[likely]] {
            job.result = *fetched;
            return false;
        } else if (fetched.error() != make_error_code(RdmaErrc::kNotReady)) {
            job.error = fetched.error();
            return false;
        }

        if (auto waited = channel_.connection().WaitReceive(seq_, 0); !waited) [[unlikely]] {
            job.error = waited.error();
            return false;
        }
        if (auto fetched = channel_.connection().FetchReceive(seq_); fetched) [[likely]] {
            job.result = *fetched;
            return false;
        }

        channel_.Park(nbio::async::Coroutine::FromHandle(handle));
        channel_.Arm();
        return true;
    }

    RdmaResult<RdmaReceiveResult> await_resume() const {
        auto& job = channel_.job();
        if (job.error) [[unlikely]] {
            return nbio::utility::unexpected(std::exchange(job.error, {}));
        }
        channel_.connection().DropReceive(job.result.seq);
        return job.result;
    }

   private:
    RdmaReceiveChannel& channel_;
    RdmaSeq seq_;
};
}  // namespace detail

RdmaReceiveChannel::RdmaReceiveChannel(nbio::net::RdmaConnector& connection, nbio::core::Multiplexer& multiplexer,
                                       nbio::async::Scheduler& scheduler)
    : nbio::core::Channel<RdmaReceiveChannel>(nbio::core::ChannelType::kRdmaReceive,
                                              static_cast<std::uintptr_t>(connection.receive_completion_channel_handle()),
                                              multiplexer, scheduler),
      connection_(connection) {
    // Registered on the first arm(): nothing to watch until a pull is parked.
}

RdmaReceiveChannel::~RdmaReceiveChannel() noexcept { multiplexer_.DeleteChannel(this); }

RdmaResult<RdmaSeq> RdmaReceiveChannel::Post(std::span<char> buffer) noexcept { return connection_.PostReceive(buffer); }

nbio::async::Task<RdmaResult<RdmaReceiveResult>> RdmaReceiveChannel::Wait(RdmaSeq seq) {
    co_return co_await detail::RdmaReceiveWaitAwaiter{*this, seq};
}

RdmaReceiveChannel::Payload& RdmaReceiveChannel::Submit() { return payload_; }

void RdmaReceiveChannel::Complete() {
    auto& payload = payload_;
    payload.ReleasePoll();

    if (!waiter_) [[unlikely]] {
        return;
    }

    auto& job = job_;
    auto waited = connection_.WaitReceive(job.seq, 0);
    if (!waited) [[unlikely]] {
        job.error = waited.error();
    } else if (*waited >= job.seq) {
        if (auto fetched = connection_.FetchReceive(job.seq); fetched) [[likely]] {
            job.result = *fetched;
        } else {
            job.error = fetched.error();
        }
    } else {
        Arm();
        return;
    }

    auto waiter = std::exchange(waiter_, {});
    scheduler_.Submit(std::move(waiter));
}
}  // namespace nbio::net

#endif  // defined(NBIO_ENABLE_RDMA) && defined(__linux__)
