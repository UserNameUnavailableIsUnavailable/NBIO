#if defined(NBIO_ENABLE_RDMA) && defined(__linux__)

#include <nbio/async/coroutine.hpp>
#include <nbio/net/rdma_send_channel.hpp>
#include <utility>

namespace nbio::net {
namespace detail {
class RdmaSendWaitAwaiter {
   public:
    RdmaSendWaitAwaiter(RdmaSendChannel& channel, RdmaSeq seq) : channel_(channel), seq_(seq) {}

    bool await_ready() const noexcept { return false; }

    template <typename PromiseType>
    bool await_suspend(std::coroutine_handle<PromiseType> handle) {
        auto& job = channel_.job();
        job.seq = seq_;
        job.mark = 0;
        job.error.clear();

        auto waited = channel_.communication().WaitSend(seq_, 0);
        if (!waited) [[unlikely]] {
            job.error = waited.error();
            return false;
        }
        job.mark = *waited;
        if (*waited >= seq_) {
            return false;
        }

        channel_.Park(nbio::async::Coroutine::FromHandle(handle));
        channel_.Arm();
        return true;
    }

    RdmaResult<RdmaSeq> await_resume() const {
        auto& job = channel_.job();
        if (job.error) [[unlikely]] {
            return nbio::utility::unexpected(std::exchange(job.error, {}));
        }
        return job.mark;
    }

   private:
    RdmaSendChannel& channel_;
    RdmaSeq seq_;
};
}  // namespace detail

RdmaSendChannel::RdmaSendChannel(nbio::net::RdmaCommunication& communication, nbio::core::Multiplexer& multiplexer,
                                 nbio::async::Scheduler& scheduler)
    : nbio::core::Channel<RdmaSendChannel>(nbio::core::ChannelType::kRdmaSend,
                                           static_cast<std::uintptr_t>(communication.completion_channel_handle()),
                                           multiplexer, scheduler),
      communication_(communication) {}

RdmaSendChannel::~RdmaSendChannel() noexcept { multiplexer_.DeleteChannel(this); }

RdmaResult<RdmaSeq> RdmaSendChannel::SubmitSend(std::span<const char> buffer) noexcept {
    return communication_.SubmitSend(buffer);
}

RdmaResult<RdmaSendResult> RdmaSendChannel::PeekSend(RdmaSeq seq) noexcept { return communication_.PeekSend(seq); }

void RdmaSendChannel::CompleteSend(RdmaSeq seq) noexcept { communication_.CompleteSend(seq); }

nbio::async::Task<RdmaResult<RdmaSeq>> RdmaSendChannel::WaitSend(RdmaSeq seq) {
    co_return co_await detail::RdmaSendWaitAwaiter{*this, seq};
}

RdmaSendChannel::Payload& RdmaSendChannel::Submit() { return payload_; }

void RdmaSendChannel::Complete() {
    payload_.ReleasePoll();
    if (!waiter_) [[unlikely]] {
        return;
    }

    auto& job = job_;
    auto waited = communication_.WaitSend(job.seq, 0);
    if (!waited) [[unlikely]] {
        job.error = waited.error();
    } else {
        job.mark = *waited;
        job.error.clear();
        if (*waited < job.seq) {
            Arm();
            return;
        }
    }

    auto waiter = std::exchange(waiter_, {});
    scheduler_.Submit(std::move(waiter));
}
}  // namespace nbio::net

#endif  // defined(NBIO_ENABLE_RDMA) && defined(__linux__)
