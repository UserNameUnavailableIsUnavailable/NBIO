#pragma once

#if defined(NBIO_ENABLE_RDMA) && defined(__linux__)

#include <cstdint>
#include <nbio/net/address.hpp>
#include <nbio/net/rdma_connector.hpp>
#include <nbio/net/rdma_resource.hpp>
#include <nbio/net/rdma_result.hpp>
#include <nbio/utility/expected.hpp>
#include <optional>

namespace nbio::net {
/**
 * @brief Admits connections on a resource's device, handing each one back prepared.
 *
 * @warning This is infrastructure, not the interface to build against. It is a
 *          primitive: @ref Accept waits with a blocking @c poll(2) on its own event
 *          fd and knows nothing about the multiplexer. Use @ref RdmaListenService
 *          for an accept loop the event loop drives. See @c docs/rdma.md.
 *
 * @details The connection a request produces is prepared, not accepted: its queue
 * pair exists and can be posted to, and the peer is still waiting. That gap is the
 * window a receiver needs, and it is also where a server decides to refuse.
 *
 * This class is designed to be used in a single thread.
 */
class RdmaListener {
   public:
    using Handle = int;

    /**
     * @brief Creates a listener on @p resource's device.
     *
     * @param resource The device, domain and regions every admitted connection is
     *        built from. Must outlive the listener and every connection it hands out.
     * @throws std::runtime_error if the event channel or the id cannot be created.
     */
    explicit RdmaListener(RdmaResource& resource);
    ~RdmaListener() noexcept;

    RdmaListener(const RdmaListener&) = delete;
    RdmaListener& operator=(const RdmaListener&) = delete;

    RdmaResult<void> Listen(net::Address address, int backlog = 4096) noexcept;

    /**
     * @brief A connection that has asked to be admitted.
     *
     * @details The connection comes back **prepared, not established**: its queue
     * pair exists, so receives can be posted to it, and the peer is still waiting.
     * @ref RdmaConnector::Establish is what accepts it. That gap is the window a
     * receiver needs, and it is also where a server decides to refuse -- letting
     * the connector go rejects the request.
     *
     * In blocking mode this waits until a request arrives, like @c accept(2). In
     * non-blocking mode it answers at once.
     *
     * @return The prepared connection; @ref RdmaErrc::kNotReady when nobody has
     *         asked yet and the listener is non-blocking; or why a request that did
     *         arrive could not be turned into a connection.
     */
    RdmaResult<RdmaConnector> Accept() noexcept;

    /**
     * @brief Whether @ref Accept may come back empty-handed.
     *
     * @details In the default, blocking mode @ref Accept waits for a connection
     * request, like @c accept(2) on a blocking socket. In non-blocking mode it
     * answers @ref RdmaErrc::kNotReady when nobody has asked yet, which is also
     * what a channel wants: the channel parks on the event fd instead of a thread.
     *
     * @param enabled Whether to stop waiting.
     */
    void NonBlocking(bool enabled = true) noexcept { non_blocking_ = enabled; }

    /** @brief Whether @ref Accept may return @ref RdmaErrc::kNotReady. */
    bool is_non_blocking() const noexcept { return non_blocking_; }
    RdmaResult<void> ReuseAddress(bool enabled = true) noexcept;

    Handle event_channel_handle() const noexcept {
        return event_channel_ ? static_cast<std::uintptr_t>(event_channel_->fd) : -1;
    }

   private:
    RdmaResource* resource_{nullptr};
    ::rdma_cm_id* communication_id_{nullptr};  // the listener itself, never a connection
    ::rdma_event_channel* event_channel_{nullptr};
    net::Address address_;
    bool non_blocking_{false};
};
}  // namespace nbio::net

#endif  // defined(NBIO_ENABLE_RDMA) && defined(__linux__)
