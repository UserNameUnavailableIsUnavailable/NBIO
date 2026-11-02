#if defined(NBIO_ENABLE_RDMA) && defined(__linux__)

#include <nbio/async/coroutine.hpp>
#include <nbio/net/rdma_listen_channel.hpp>
#include <utility>

namespace nbio::net {
namespace detail {
class RdmaAcceptAwaiter {
   public:
    explicit RdmaAcceptAwaiter(RdmaListenChannel& channel) : channel_(channel) {}

    bool await_ready() const noexcept { return false; }

    template <typename PromiseType>
    bool await_suspend(std::coroutine_handle<PromiseType> handle) {
        auto& job = channel_.job();
        job.communication.reset();
        job.error.clear();

        auto accepted = channel_.Advance();
        if (accepted) [[likely]] {
            job.communication = std::move(*accepted);
            return false;
        }
        if (accepted.error() != make_error_code(RdmaErrc::kNotReady)) [[unlikely]] {
            job.error = accepted.error();
            return false;
        }

        channel_.Park(nbio::async::Coroutine::FromHandle(handle));
        channel_.Arm();
        return true;
    }

    RdmaResult<RdmaCommunication> await_resume() const {
        auto& job = channel_.job();
        if (job.error) [[unlikely]] {
            return nbio::utility::unexpected(std::exchange(job.error, {}));
        }
        if (job.communication) {
            return RdmaCommunication(std::move(*job.communication));
        }
        return nbio::utility::unexpected(make_error_code(RdmaErrc::kOperationFailed));
    }

   private:
    RdmaListenChannel& channel_;
};
}  // namespace detail

RdmaListenChannel::RdmaListenChannel(nbio::net::RdmaListener& listener, nbio::core::Multiplexer& multiplexer,
                                     nbio::async::Scheduler& scheduler)
    : nbio::core::Channel<RdmaListenChannel>(nbio::core::ChannelType::kRdmaAccept,
                                             static_cast<std::uintptr_t>(listener.event_channel_handle()), multiplexer,
                                             scheduler),
      listener_(listener) {}

RdmaListenChannel::~RdmaListenChannel() noexcept { multiplexer_.DeleteChannel(this); }

nbio::async::Task<RdmaResult<RdmaCommunication>> RdmaListenChannel::Accept() {
    co_return co_await detail::RdmaAcceptAwaiter{*this};
}

RdmaResult<RdmaCommunication> RdmaListenChannel::Advance() noexcept { return listener_.TryAccept(); }

RdmaListenChannel::Payload& RdmaListenChannel::Submit() { return payload_; }

void RdmaListenChannel::Complete() {
    payload_.ReleasePoll();
    if (!waiter_) [[unlikely]] {
        return;
    }

    auto& job = job_;
    auto accepted = Advance();
    if (!accepted) [[unlikely]] {
        job.communication.reset();
        if (accepted.error() == make_error_code(RdmaErrc::kNotReady)) {
            job.error.clear();
            Arm();
            return;
        }
        job.error = accepted.error();
    } else {
        job.communication = std::move(*accepted);
        job.error.clear();
    }

    auto waiter = std::exchange(waiter_, {});
    scheduler_.Submit(std::move(waiter));
}
}  // namespace nbio::net

#endif  // defined(NBIO_ENABLE_RDMA) && defined(__linux__)
