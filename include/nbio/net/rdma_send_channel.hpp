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
 * @brief The sending half of a connection, as a coroutine-facing channel.
 *
 * @details This adds nothing to the connector except a way to suspend on it: a
 * post is a work request, and a wait is one sequence number followed by taking
 * and releasing its result. The channel owns no memory and keeps no buffer: the
 * spans it is given are the caller's regions, and the span a wait answers is the
 * one that was posted.
 *
 * @note One waiter at a time. The channel parks a single coroutine, so two
 *       coroutines awaiting @ref Wait on the same channel would overwrite each
 *       other's job. A single producer coroutine per direction is the intended
 *       shape.
 *
 * This class is designed to be used in a single thread.
 */
class RdmaSendChannel final : public nbio::core::Channel<RdmaSendChannel> {
   public:
    using Handle = int;
    using Payload = detail::PollPayload<RdmaSendChannel>;

    /**
     * @brief What one parked wait is waiting for, and what it found.
     */
    struct Pending {
        RdmaSeq seq{0};             ///< The sequence number being waited for.
        RdmaSendResult result{};    ///< The result, once it is ready.
        std::error_code error{};    ///< Why it will not become ready, if it will not.
    };

    /**
     * @brief Registers this channel on the connection's send completion fd.
     *
     * @param connection The connection whose send queue this channel drives.
     * @param multiplexer The backend that watches the fd.
     * @param scheduler The scheduler that resumes a parked waiter.
     */
    RdmaSendChannel(nbio::net::RdmaConnector& connection, nbio::core::Multiplexer& multiplexer,
                    nbio::async::Scheduler& scheduler);

    ~RdmaSendChannel() noexcept;

    /**
     * @brief Posts one SEND, exactly as @ref RdmaConnector::PostSend does.
     *
     * @param buffer A subspan of the connection's send region.
     * @return The sequence number assigned to the request, or why it was not
     *         posted.
     */
    RdmaResult<RdmaSeq> Post(std::span<const char> buffer) noexcept;

    /**
     * @brief Waits for one sequence number's result, then consumes it.
     *
     * @details Waits, fetches and drops: the result is handed out exactly once, and
     * the slot is released so the ring can advance. A second wait for the same
     * sequence number fails with @ref RdmaErrc::kInvalidSequence.
     *
     * @param seq The sequence number returned by @ref Post.
     * @return The retired buffer and its sequence number, or why it never retired.
     */
    nbio::async::Task<RdmaResult<RdmaSendResult>> Wait(RdmaSeq seq);

    /** @brief The backend's one-shot poll payload. */
    Payload& Submit();

    /** @brief Reaps the completion queue and resumes the parked waiter, if any. */
    void Complete();

    /** @brief Remembers the coroutine a completed wait should resume. */
    void Park(nbio::async::Coroutine waiter) noexcept { waiter_ = std::move(waiter); }

    /** @brief The connection this channel drives. */
    nbio::net::RdmaConnector& connection() noexcept { return connection_; }

    /** @brief The job a parked pull is working on. */
    Pending& job() noexcept { return job_; }

    /** @brief The job a parked pull is working on. */
    const Pending& job() const noexcept { return job_; }

   private:
    nbio::net::RdmaConnector& connection_;
    Pending job_{};
    nbio::async::Coroutine waiter_{};
    Payload payload_{};
};
}  // namespace nbio::net

#endif  // defined(NBIO_ENABLE_RDMA) && defined(__linux__)
