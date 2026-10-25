#pragma once
#if defined(NBIO_ENABLE_RDMA) && defined(__linux__)

#include <nbio/async/scheduler.hpp>
#include <nbio/core/multiplexer.hpp>
#include <nbio/net/rdma_connect_channel.hpp>
#include <nbio/net/rdma_connector.hpp>
#include <nbio/net/rdma_receive_channel.hpp>
#include <nbio/net/rdma_result.hpp>
#include <nbio/net/rdma_send_channel.hpp>

namespace nbio::net {
/**
 * @brief One established connection, with its two coroutine-facing channels.
 *
 * @details A session owns the connector and the two channels built on it, and
 * nothing else: no buffer, no window, no policy. What a caller posts and when it
 * takes the result back is the caller's business.
 *
 * The channels reference the connector, so this object may not be moved once it
 * has been constructed.
 */
class RdmaSessionService final {
   public:
    /**
     * @brief Takes ownership of an established connection and builds its channels.
     *
     * @param connection The connection to drive. Moved in.
     * @param multiplexer The backend the channels are registered on.
     * @param scheduler The scheduler that resumes a parked puller.
     */
    RdmaSessionService(nbio::net::RdmaConnector connection, nbio::core::Multiplexer& multiplexer,
                       nbio::async::Scheduler& scheduler);

    RdmaSessionService(const RdmaSessionService&) = delete;
    RdmaSessionService& operator=(const RdmaSessionService&) = delete;
    RdmaSessionService(RdmaSessionService&&) = delete;
    RdmaSessionService& operator=(RdmaSessionService&&) = delete;
    ~RdmaSessionService() noexcept = default;

    /** @brief The connection itself, for the raw sequence-number API. */
    nbio::net::RdmaConnector& connection() noexcept { return connection_; }

    /** @brief The connection itself, for the raw sequence-number API. */
    const nbio::net::RdmaConnector& connection() const noexcept { return connection_; }

    /** @brief The device, domain and regions this connection is built from. */
    nbio::net::RdmaResource& resource() noexcept { return connection_.resource(); }

    /** @brief The device, domain and regions this connection is built from. */
    const nbio::net::RdmaResource& resource() const noexcept { return connection_.resource(); }

    /**
     * @brief Submits the connection: rdma_connect on the active end, rdma_accept on
     *        the passive one, then the established event.
     *
     * @details A session handed out by @ref RdmaConnectService or
     * @ref RdmaListenService is prepared, not established: its queue pair exists, so
     * the receive channel can be posted to, and the peer cannot send yet. This is
     * the call that lets the peer send, and it must come after everything that has
     * to be posted first.
     *
     * @return Nothing once the connection is up, or why it is not. Answers nothing
     *         when the connection is already established.
     */
    nbio::async::Task<RdmaResult<void>> Establish();

    /** @brief Whether the connection has been established. */
    bool is_established() const noexcept { return connection_.is_established(); }

    /** @brief Whether the queue pair exists, and so whether a receive may be posted. */
    bool is_prepared() const noexcept { return connection_.is_prepared(); }

    /** @brief The channel that posts sends and pulls their results. */
    RdmaSendChannel& send_channel() noexcept { return send_channel_; }

    /** @brief The channel that posts sends and pulls their results. */
    const RdmaSendChannel& send_channel() const noexcept { return send_channel_; }

    /** @brief The channel that posts receives and pulls their results. */
    RdmaReceiveChannel& receive_channel() noexcept { return receive_channel_; }

    /** @brief The channel that posts receives and pulls their results. */
    const RdmaReceiveChannel& receive_channel() const noexcept { return receive_channel_; }

    /**
     * @brief Posts the whole of @p buffer as one SEND.
     *
     * @details The same call as @c send_channel().Post, for a caller that does not
     * want to reach through the channel.
     *
     * @param buffer A subspan of the resource's send region.
     * @return The sequence number of the request, to be given to @ref WaitSend, or
     *         why it was not posted.
     */
    RdmaResult<RdmaSeq> PostSend(std::span<const char> buffer) noexcept { return send_channel_.Post(buffer); }

    /**
     * @brief Waits for one send's completion and consumes it.
     *
     * @param seq The sequence number @ref PostSend answered.
     * @return The retired buffer, whose memory is the caller's again, or why it
     *         never retired.
     */
    nbio::async::Task<RdmaResult<RdmaSendResult>> WaitSend(RdmaSeq seq) { return send_channel_.Wait(seq); }

    /**
     * @brief Posts the whole of @p buffer as one RECV.
     *
     * @details A receive has to be posted before the peer sends, which is why this
     * can be called as soon as the session exists and before @ref Establish.
     *
     * @param buffer A subspan of the resource's receive region.
     * @return The sequence number of the request, to be given to @ref WaitReceive,
     *         or why it was not posted.
     */
    RdmaResult<RdmaSeq> PostReceive(std::span<char> buffer) noexcept { return receive_channel_.Post(buffer); }

    /**
     * @brief Waits for one receive's completion and consumes it.
     *
     * @param seq The sequence number @ref PostReceive answered.
     * @return The buffer, how many bytes the peer wrote, and the sequence number; or
     *         why nothing arrived.
     */
    nbio::async::Task<RdmaResult<RdmaReceiveResult>> WaitReceive(RdmaSeq seq) { return receive_channel_.Wait(seq); }

   private:
    // Declared first: every channel references it.
    nbio::net::RdmaConnector connection_;
    RdmaSendChannel send_channel_;
    RdmaReceiveChannel receive_channel_;
    RdmaConnectChannel connect_channel_;
};
}  // namespace nbio::net

#endif  // defined(NBIO_ENABLE_RDMA) && defined(__linux__)
