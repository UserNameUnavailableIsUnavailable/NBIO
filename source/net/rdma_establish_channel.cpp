#if defined(NBIO_ENABLE_RDMA) && defined(__linux__)

#include <nbio/async/coroutine.hpp>
#include <nbio/net/rdma_establish_channel.hpp>
#include <utility>

namespace nbio::net {
namespace detail {
class RdmaEstablishAwaiter {
   public:
    explicit RdmaEstablishAwaiter(RdmaEstablishChannel& channel) : channel_(channel) {}

    bool await_ready() const noexcept { return false; }

    template <typename PromiseType>
    bool await_suspend(std::coroutine_handle<PromiseType> handle) {
        auto& job = channel_.job();
        job.error.clear();

        auto step = channel_.Advance();
        if (step) {
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
    RdmaEstablishChannel& channel_;
};
}  // namespace detail

RdmaEstablishChannel::RdmaEstablishChannel(nbio::net::RdmaCommunication& communication,
                                           nbio::core::Multiplexer& multiplexer, nbio::async::Scheduler& scheduler)
    : nbio::core::Channel<RdmaEstablishChannel>(nbio::core::ChannelType::kRdmaEstablish,
                                                static_cast<std::uintptr_t>(communication.event_channel_handle()),
                                                multiplexer, scheduler),
      communication_(communication) {}

RdmaEstablishChannel::~RdmaEstablishChannel() noexcept { multiplexer_.DeleteChannel(this); }

RdmaResult<void> RdmaEstablishChannel::Advance() noexcept { return communication_.AdvanceEstablish(0); }

nbio::async::Task<RdmaResult<void>> RdmaEstablishChannel::Establish() {
    co_return co_await detail::RdmaEstablishAwaiter{*this};
}

RdmaEstablishChannel::Payload& RdmaEstablishChannel::Submit() { return payload_; }

void RdmaEstablishChannel::Complete() {
    payload_.ReleasePoll();
    if (!waiter_) [[unlikely]] {
        return;
    }

    auto& job = job_;
    auto step = communication_.AdvanceEstablish(0);
    if (!step) [[unlikely]] {
        if (step.error() == make_error_code(RdmaErrc::kNotReady)) {
            job.error.clear();
            Arm();
            return;
        }
        job.error = step.error();
    } else {
        job.error.clear();
    }

    auto waiter = std::exchange(waiter_, {});
    scheduler_.Submit(std::move(waiter));
}
}  // namespace nbio::net

#endif  // defined(NBIO_ENABLE_RDMA) && defined(__linux__)
