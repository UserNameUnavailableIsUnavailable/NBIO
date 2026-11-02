#pragma once
#if defined(NBIO_ENABLE_RDMA) && defined(__linux__)

#include <nbio/net/address.hpp>
#include <nbio/net/rdma_communication.hpp>
#include <nbio/net/rdma_result.hpp>

struct rdma_cm_id;
struct rdma_event_channel;

namespace nbio::net {
class RdmaConnectChannel;

/**
 * @brief The control plane of an outgoing connection.
 *
 * @details A connector owns the communication-manager id and its event channel
 * until @ref Connect has resolved the address and the route, built the queue
 * pair, and handed everything over to the @ref RdmaCommunication it returns. A
 * connector therefore produces at most one communication.
 *
 * @warning This is a primitive, not the interface to build against. Its
 * operations block and it knows nothing about the multiplexer. Use
 * @ref RdmaConnectService and reach here only for direct control.
 */
class RdmaConnector {
    friend class RdmaConnectChannel;

   public:
    /**
     * @brief Creates the id and its event channel. Nothing is connected or bound.
     *
     * @throws std::system_error if the device refuses an id or a channel.
     */
    explicit RdmaConnector(RdmaResource& resource);

    RdmaConnector(const RdmaConnector&) = delete;
    RdmaConnector& operator=(const RdmaConnector&) = delete;
    RdmaConnector(RdmaConnector&& other) noexcept;
    RdmaConnector& operator=(RdmaConnector&& other) noexcept;
    ~RdmaConnector() noexcept;

    friend void swap(RdmaConnector& lhs, RdmaConnector& rhs) noexcept;

    /**
     * @brief Pins the local address the connection comes from.
     *
     * @details Calls rdma_bind_addr and returns immediately. Optional: an unbound
     *          connector lets the communication manager choose the source.
     */
    RdmaResult<void> Bind(const net::Address& source) noexcept;

    /**
     * @brief Resolves the address and route, builds the queue pair, and returns the
     *        prepared @ref RdmaCommunication.
     *
     * @details rdma_resolve_addr and rdma_resolve_route return immediately; their
     *          completion events are then awaited on the event channel with the
     *          deadlines set by @ref SetAddressResolveTimeout and
     *          @ref SetRouteResolveTimeout.
     *
     * @param target The address to connect to.
     *
     * @return The prepared communication, whose receive queue can be posted to and
     *         whose @ref RdmaCommunication::Establish finishes the connection; or
     *         why it could not be prepared. The connector is spent either way.
     */
    RdmaResult<RdmaCommunication> Connect(const net::Address& target) noexcept;

    /**
     * @brief Sets the deadline for the address resolve and its completion event.
     *
     * @param timeout_ms Milliseconds; -1 waits indefinitely, 0 falls back to the
     *        default. The default is 2500.
     */
    void SetAddressResolveTimeout(int timeout_ms) noexcept {
        address_resolve_timeout_ms_ = timeout_ms == 0 ? 2500 : timeout_ms;
    }

    /**
     * @brief Sets the deadline for the route resolve and its completion event.
     *
     * @param timeout_ms Milliseconds; -1 waits indefinitely, 0 falls back to the
     *        default. The default is 2500.
     */
    void SetRouteResolveTimeout(int timeout_ms) noexcept {
        route_resolve_timeout_ms_ = timeout_ms == 0 ? 2500 : timeout_ms;
    }

    /** @brief The event channel's fd, for the multiplexer to poll. */
    int event_channel_handle() const noexcept;

   private:
    // The non-blocking steps the connect channel drives through the multiplexer:
    // SubmitResolve kicks off the address resolve, AdvanceResolve processes one
    // resolution event, CompleteResolve builds the queue pair and hands it over.
    RdmaResult<void> SubmitResolve(const net::Address& target) noexcept;
    RdmaResult<void> AdvanceResolve(int poll_timeout_ms) noexcept;
    RdmaResult<RdmaCommunication> CompleteResolve() noexcept;
    bool IsResolved() const noexcept { return phase_ == 3; }
    void Release() noexcept;

    ::rdma_cm_id* communication_id_{nullptr};
    ::rdma_event_channel* event_channel_{nullptr};
    RdmaResource* resource_{nullptr};
    // The deadline for the address resolve and its completion event.
    int address_resolve_timeout_ms_{2500};
    // The deadline for the route resolve and its completion event.
    int route_resolve_timeout_ms_{2500};
    // 0 idle, 1 address resolve submitted, 2 route resolve submitted, 3 resolved.
    int phase_{0};
};
}  // namespace nbio::net

#endif  // defined(NBIO_ENABLE_RDMA) && defined(__linux__)
