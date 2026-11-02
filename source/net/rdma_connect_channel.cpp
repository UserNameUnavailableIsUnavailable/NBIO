#if defined(NBIO_ENABLE_RDMA) && defined(__linux__)

#include <nbio/async/coroutine.hpp>
#include <nbio/net/rdma_connect_channel.hpp>
#include <utility>

namespace nbio::net {
namespace detail {
class RdmaConnectAwaiter {
   public:
    RdmaConnectAwaiter(RdmaConnectChannel& channel, const net::Address& target) : channel_(channel), target_(target) {}

    bool await_ready() const noexcept { return false; }

    template <typename PromiseType>
    bool await_suspend(std::coroutine_handle<PromiseType> handle) {
        auto& job = channel_.job();
        job.communication.reset();
        job.error.clear();

        if (auto submitted = channel_.SubmitResolve(target_); !submitted) [[unlikely]] {
            job.error = submitted.error();
            return false;
        }
        auto step = channel_.Advance();
        if (step) [[likely]] {
            job.communication = std::move(*step);
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
    RdmaConnectChannel& channel_;
    net::Address target_;
};
}  // namespace detail

RdmaConnectChannel::RdmaConnectChannel(nbio::net::RdmaConnector& connector, nbio::core::Multiplexer& multiplexer,
                                       nbio::async::Scheduler& scheduler)
    : nbio::core::Channel<RdmaConnectChannel>(nbio::core::ChannelType::kRdmaConnect,
                                              static_cast<std::uintptr_t>(connector.event_channel_handle()), multiplexer,
                                              scheduler),
      connector_(connector) {}

RdmaConnectChannel::~RdmaConnectChannel() noexcept { multiplexer_.DeleteChannel(this); }

RdmaResult<void> RdmaConnectChannel::SubmitResolve(const net::Address& target) noexcept {
    return connector_.SubmitResolve(target);
}

RdmaResult<RdmaCommunication> RdmaConnectChannel::Advance() noexcept {
    if (auto advanced = connector_.AdvanceResolve(0); !advanced) [[unlikely]] {
        return utility::unexpected(advanced.error());
    }
    if (!connector_.IsResolved()) [[unlikely]] {
        return utility::unexpected(make_error_code(RdmaErrc::kNotReady));
    }
    return connector_.CompleteResolve();
}

nbio::async::Task<RdmaResult<RdmaCommunication>> RdmaConnectChannel::Connect(const net::Address& target) {
    co_return co_await detail::RdmaConnectAwaiter{*this, target};
}

RdmaConnectChannel::Payload& RdmaConnectChannel::Submit() { return payload_; }

void RdmaConnectChannel::Complete() {
    payload_.ReleasePoll();
    if (!waiter_) [[unlikely]] {
        return;
    }

    auto& job = job_;
    auto step = Advance();
    if (step) {
        job.communication = std::move(*step);
        job.error.clear();
    } else if (step.error() == make_error_code(RdmaErrc::kNotReady)) {
        Arm();
        return;
    } else {
        job.error = step.error();
    }

    auto waiter = std::exchange(waiter_, {});
    scheduler_.Submit(std::move(waiter));
}
}  // namespace nbio::net

#endif  // defined(NBIO_ENABLE_RDMA) && defined(__linux__)
