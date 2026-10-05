#if defined(NBIO_ENABLE_RDMA) && defined(__linux__)

#include <nbio/net/RdmaAcceptChannel.hpp>
#include <nbio/async/Coroutine.hpp>
#include <stdexcept>
#include <system_error>
#include <utility>

namespace nbio::net {
namespace detail {
class RdmaAcceptAwaiter {
   public:
    explicit RdmaAcceptAwaiter(RdmaAcceptChannel& channel) : channel_(channel) {}

    bool await_ready() const noexcept { return false; }

    template <typename PromiseType>
    bool await_suspend(std::coroutine_handle<PromiseType> handle) {
        channel_.job().connection.reset();
        channel_.job().error.clear();
        channel_.Park(nbio::async::Coroutine::FromHandle(handle));
        channel_.Arm();
        return true;
    }

    RdmaResult<RdmaConnector> await_resume() {
        auto& job = channel_.job();
        if (job.error) {
            return nbio::utility::unexpected(std::exchange(job.error, std::error_code{}));
        }
        if (job.connection) {
            return std::move(*job.connection);
        }
        return nbio::utility::unexpected(make_error_code(RdmaErrc::kOperationFailed));
    }

   private:
    RdmaAcceptChannel& channel_;
};
}  // namespace detail

RdmaAcceptChannel::RdmaAcceptChannel(nbio::net::RdmaAcceptor& acceptor, nbio::core::Multiplexer& multiplexer,
                                     nbio::async::Scheduler& scheduler)
    : nbio::core::Channel<RdmaAcceptChannel>(nbio::core::ChannelType::kRdmaAccept,
                                                   static_cast<std::uintptr_t>(acceptor.event_channel_handle()), multiplexer,
                                                   scheduler),
      acceptor_(acceptor) {
    // The acceptor has to be polled rather than waited on, and this is the one
    // failure the channel cannot report through a job -- there is no waiter to
    // report it to yet. A constructor is the one place still allowed to throw.
    if (auto armed = acceptor_.NonBlocking(true); !armed) [[unlikely]]
    {
        throw std::system_error(armed.error(), "Failed to make the rdma acceptor non-blocking");
    }
    // Registered on the first arm(): nothing to watch until a wait queues.
}

RdmaAcceptChannel::~RdmaAcceptChannel() noexcept { multiplexer_.DeleteChannel(this); }

nbio::async::Task<nbio::Runtime, RdmaResult<RdmaConnector>> RdmaAcceptChannel::accept() {
    co_return co_await detail::RdmaAcceptAwaiter{*this};
}

RdmaAcceptChannel::Payload& RdmaAcceptChannel::Submit() { return payload_; }

void RdmaAcceptChannel::Complete() {
    auto& payload = payload_;
    payload.release_poll();

    if (!waiter_) [[unlikely]] {
        return;
    }

    auto accepted = acceptor_.Accept();
    if (!accepted) [[unlikely]] {
        job().connection.reset();
        job().error = accepted.error();
    } else if (accepted->state == RdmaAcceptState::kWouldBlock) {
        // Nothing has asked to be admitted yet -- or the event was about a
        // connection that is already established. Either way the waiter stays
        // Parked and the channel keeps watching.
        job().connection.reset();
        job().error.clear();
        Arm();
        return;
    } else {
        if (accepted->connection) {
            job().connection = std::move(*accepted->connection);
        } else {
            job().error = make_error_code(RdmaErrc::kOperationFailed);
        }
        if (job().error) {
            auto waiter = std::exchange(waiter_, {});
            scheduler_.Submit(std::move(waiter));
            return;
        }
        job().error.clear();
    }

    auto waiter = std::exchange(waiter_, {});
    scheduler_.Submit(std::move(waiter));
}
}  // namespace nbio::net
#endif  // defined(NBIO_ENABLE_RDMA) && defined(__linux__)