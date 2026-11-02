#pragma once
#if defined(NBIO_ENABLE_RDMA) && defined(__linux__)

#include <cstdint>
#include <nbio/net/address.hpp>
#include <nbio/net/rdma_communication.hpp>
#include <nbio/net/rdma_resource.hpp>
#include <nbio/net/rdma_result.hpp>

struct rdma_cm_id;
struct rdma_event_channel;

namespace nbio::net {
class RdmaListenChannel;

/**
 * @brief The control plane of a listening address.
 *
 * @details Binds an address, listens, and turns each connection request into a
 * prepared @ref RdmaCommunication whose queue pair exists but whose connection is
 * not yet accepted -- that gap is where a receiver posts, and where a server
 * decides to refuse.
 *
 * @warning This is a primitive, not the interface to build against. Its
 * operations block and it knows nothing about the multiplexer. Use
 * @ref RdmaListenService and reach here only for direct control.
 */
class RdmaListener {
    friend class RdmaListenChannel;

   public:
    using Handle = int;

    explicit RdmaListener(RdmaResource& resource);
    ~RdmaListener() noexcept;

    RdmaListener(const RdmaListener&) = delete;
    RdmaListener& operator=(const RdmaListener&) = delete;

    /** @brief Binds a local address to listen on. */
    RdmaResult<void> Bind(const net::Address& address) noexcept;

    /** @brief Starts listening, with the given backlog. */
    RdmaResult<void> Listen(int backlog = 4096) noexcept;

    /** @brief Waits for a connection request and returns it prepared, not accepted. */
    RdmaResult<RdmaCommunication> Accept() noexcept;

    Handle event_channel_handle() const noexcept;

    // The port the id is bound to, in host byte order; 0 before Bind. Binding
    // with port 0 lets the provider choose one, and this is how the caller
    // learns which one it chose.
    std::uint16_t BoundPort() const noexcept;

   private:
    // One non-blocking attempt: kNotReady when nobody has asked yet.
    RdmaResult<RdmaCommunication> TryAccept() noexcept;

    RdmaResource* resource_{nullptr};
    ::rdma_cm_id* communication_id_{nullptr};
    ::rdma_event_channel* event_channel_{nullptr};
};
}  // namespace nbio::net

#endif  // defined(NBIO_ENABLE_RDMA) && defined(__linux__)
