#pragma once
#if defined(NBIO_ENABLE_RDMA) && defined(__linux__)

#include <memory>
#include <nbio/async/coroutine.hpp>
#include <nbio/async/scheduler.hpp>
#include <nbio/async/task.hpp>
#include <nbio/core/channel.hpp>
#include <nbio/net/payload.hpp>
#include <nbio/net/rdma_listener.hpp>
#include <optional>
#include <system_error>
#include <utility>

namespace nbio::net {
/**
 * @brief The asynchronous half of a listener: parks a coroutine and is woken by
 *        the multiplexer when the listener's fd is readable.
 */
class RdmaListenChannel final : public nbio::core::Channel<RdmaListenChannel> {
   public:
    using Handle = int;
    using Payload = detail::PollPayload<RdmaListenChannel>;

    struct PendingAccept {
        std::optional<RdmaCommunication> communication{};
        std::error_code error{};
    };

    RdmaListenChannel(nbio::net::RdmaListener& listener, nbio::core::Multiplexer& multiplexer,
                      nbio::async::Scheduler& scheduler);
    ~RdmaListenChannel() noexcept;

    nbio::async::Task<RdmaResult<RdmaCommunication>> Accept();

    // One non-blocking attempt to accept a connection; kNotReady when none is
    // waiting. This is what the channel drives.
    RdmaResult<RdmaCommunication> Advance() noexcept;

    Payload& Submit();
    void Complete();
    void Park(nbio::async::Coroutine waiter) noexcept { waiter_ = std::move(waiter); }

    PendingAccept& job() noexcept { return job_; }
    const PendingAccept& job() const noexcept { return job_; }
    nbio::net::RdmaListener& listener() noexcept { return listener_; }

   private:
    nbio::net::RdmaListener& listener_;
    PendingAccept job_{};
    nbio::async::Coroutine waiter_{};
    Payload payload_{};
};
}  // namespace nbio::net

#endif  // defined(NBIO_ENABLE_RDMA) && defined(__linux__)
