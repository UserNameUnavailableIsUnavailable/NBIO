#pragma once
#if defined(NBIO_ENABLE_RDMA) && defined(__linux__)

#include <nbio/async/coroutine.hpp>
#include <nbio/async/scheduler.hpp>
#include <nbio/async/task.hpp>
#include <nbio/core/channel.hpp>
#include <nbio/core/multiplexer.hpp>
#include <nbio/net/payload.hpp>
#include <nbio/net/rdma_connector.hpp>
#include <nbio/net/rdma_result.hpp>
#include <span>
#include <system_error>
#include <utility>

namespace nbio::net {
/**
 * @brief The receiving half of a connection, as a coroutine-facing channel.
 *
 * @details A post makes a receive buffer available to the device; a wait takes one
 * sequence number, its buffer and length, and releases the slot. Nothing is
 * reposted for the caller: a receive buffer that has been waited for is the
 * caller's again, and posting it once more is a decision the caller makes with
 * @ref Post.
 *
 * @note One waiter at a time, for the same reason as @ref RdmaSendChannel.
 *
 * This class is designed to be used in a single thread.
 */
class RdmaReceiveChannel final : public nbio::core::Channel<RdmaReceiveChannel> {
   public:
    using Handle = int;
    using Payload = detail::PollPayload<RdmaReceiveChannel>;

    /**
     * @brief What one parked wait is waiting for, and what it found.
     */
    struct Pending {
        RdmaSeq seq{0};             ///< The sequence number being waited for.
        RdmaReceiveResult result{}; ///< The result, once it is ready.
        std::error_code error{};    ///< Why it will not become ready, if it will not.
    };

    /**
     * @brief Registers this channel on the connection's receive completion fd.
     *
     * @param connection The connection whose receive queue this channel drives.
     * @param multiplexer The backend that watches the fd.
     * @param scheduler The scheduler that resumes a parked waiter.
     */
    RdmaReceiveChannel(nbio::net::RdmaConnector& connection, nbio::core::Multiplexer& multiplexer,
                       nbio::async::Scheduler& scheduler);

    ~RdmaReceiveChannel() noexcept;

    /**
     * @brief Posts one RECV, exactly as @ref RdmaConnector::PostReceive does.
     *
     * @param buffer A subspan of the connection's receive region.
     * @return The sequence number assigned to the request, or why it was not
     *         posted.
     */
    RdmaResult<RdmaSeq> Post(std::span<char> buffer) noexcept;

    /**
     * @brief Waits for one sequence number's result, then consumes it.
     *
     * @param seq The sequence number returned by @ref Post.
     * @return The buffer, the bytes the peer wrote into it, and the sequence
     *         number; or why it never arrived.
     */
    nbio::async::Task<RdmaResult<RdmaReceiveResult>> Wait(RdmaSeq seq);

    /** @brief The backend's one-shot poll payload. */
    Payload& Submit();

    /** @brief Reaps the completion queue and resumes the parked waiter, if any. */
    void Complete();

    /** @brief Remembers the coroutine a completed wait should resume. */
    void Park(nbio::async::Coroutine waiter) noexcept { waiter_ = std::move(waiter); }

    /** @brief The connection this channel drives. */
    nbio::net::RdmaConnector& connection() noexcept { return connection_; }

    /** @brief The job a parked wait is working on. */
    Pending& job() noexcept { return job_; }

    /** @brief The job a parked wait is working on. */
    const Pending& job() const noexcept { return job_; }

   private:
    nbio::net::RdmaConnector& connection_;
    Pending job_{};
    nbio::async::Coroutine waiter_{};
    Payload payload_{};
};
}  // namespace nbio::net

#endif  // defined(NBIO_ENABLE_RDMA) && defined(__linux__)
