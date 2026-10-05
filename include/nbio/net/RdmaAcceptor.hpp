#pragma once

#if defined(NBIO_ENABLE_RDMA) && defined(__linux__)

#include <nbio/utility/Expected.hpp>
#include <nbio/net/RdmaConnector.hpp>
#include <nbio/net/RdmaResourceManager.hpp>
#include <nbio/net/Address.hpp>
#include <nbio/net/RdmaResult.hpp>
#include <cstdint>
#include <optional>

namespace nbio::net {
enum class RdmaAcceptState {
    kAccepted,
    kWouldBlock,
};

struct RdmaAcceptResult {
    RdmaAcceptState state{RdmaAcceptState::kWouldBlock};
    std::optional<RdmaConnector> connection{};
};

class RdmaAcceptor {
   public:
    using Handle = int;
    explicit RdmaAcceptor(RdmaResourceManager& resources);
    ~RdmaAcceptor() noexcept;

    RdmaAcceptor(const RdmaAcceptor&) = delete;
    RdmaAcceptor& operator=(const RdmaAcceptor&) = delete;

    RdmaResult<void> Listen(net::Address address, int backlog = 4096) noexcept;

    // A connection that has finished its handshake, or nothing when nobody has
    // asked to be admitted.
    RdmaResult<RdmaAcceptResult> Accept() noexcept;

    RdmaResult<void> NonBlocking(bool enabled = true) noexcept;
    RdmaResult<void> ReuseAddress(bool enabled = true) noexcept;

    Handle event_channel_handle() const noexcept {
        return event_channel_ ? static_cast<std::uintptr_t>(event_channel_->fd) : -1;
    }

   private:
    RdmaResourceManager* resources_{nullptr};
    ::rdma_cm_id* communication_id_{nullptr};  // the listener itself, never a connection
    ::rdma_event_channel* event_channel_{nullptr};
    net::Address address_;
};
}  // namespace nbio::net

#endif  // defined(NBIO_ENABLE_RDMA) && defined(__linux__)

