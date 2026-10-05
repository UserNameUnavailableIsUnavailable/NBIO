#pragma once

#if defined(NBIO_ENABLE_RDMA) && defined(__linux__)

#include <NBIO/Utility/Expected.hpp>
#include <NBIO/Net/RdmaConnector.hpp>
#include <NBIO/Net/RdmaResourceManager.hpp>
#include <NBIO/Net/Address.hpp>
#include <NBIO/Net/RdmaResult.hpp>
#include <cstdint>
#include <optional>

namespace NBIO::Net {
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

    RdmaResult<void> Listen(Net::Address address, int backlog = 4096) noexcept;

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
    Net::Address address_;
};
}  // namespace NBIO::Net

#endif  // defined(NBIO_ENABLE_RDMA) && defined(__linux__)

