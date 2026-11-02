#if defined(NBIO_ENABLE_RDMA) && defined(__linux__)

#include <poll.h>
#include <rdma/rdma_cma.h>

#include <arpa/inet.h>
#include <cerrno>
#include <nbio/net/rdma_listener.hpp>
#include <stdexcept>
#include <system_error>
#include <utility>

namespace nbio::net {
namespace {
std::error_code Failing() { return {errno, std::system_category()}; }

enum class CmEventStatus { kEvent, kNone, kFailure };

// Takes one event, waiting up to timeout_ms for one to exist. Polling first is what
// lets a blocking fd be used by a channel that was woken by the multiplexer.
CmEventStatus PollCmEvent(::rdma_event_channel* channel, int timeout_ms, ::rdma_cm_event** event) noexcept {
    pollfd waiter{.fd = channel->fd, .events = POLLIN, .revents = 0};
    const int ready = ::poll(&waiter, 1, timeout_ms);
    if (ready == 0) return CmEventStatus::kNone;
    if (ready < 0) return CmEventStatus::kFailure;
    if (::rdma_get_cm_event(channel, event) == 0) return CmEventStatus::kEvent;
    if (errno == EAGAIN || errno == EWOULDBLOCK) return CmEventStatus::kNone;
    return CmEventStatus::kFailure;
}
}  // namespace

RdmaListener::RdmaListener(RdmaResource& resource) : resource_(&resource) {
    event_channel_ = ::rdma_create_event_channel();
    if (!event_channel_) [[unlikely]] {
        throw std::runtime_error("Failed to create the rdma event channel");
    }
    if (::rdma_create_id(event_channel_, &communication_id_, nullptr, RDMA_PS_TCP)) [[unlikely]] {
        ::rdma_destroy_event_channel(event_channel_);
        event_channel_ = nullptr;
        throw std::runtime_error("Failed to create the rdma id");
    }
}

RdmaResult<void> RdmaListener::Bind(const net::Address& address) noexcept {
    if (::rdma_bind_addr(communication_id_, const_cast<::sockaddr*>(address.storage<::sockaddr>()))) [[unlikely]] {
        return utility::unexpected(Failing());
    }
    if (!resource_->Serves(*communication_id_)) [[unlikely]] {
        return utility::unexpected(make_error_code(RdmaErrc::kDeviceMismatch));
    }
    return {};
}

RdmaResult<void> RdmaListener::Listen(int backlog) noexcept {
    if (::rdma_listen(communication_id_, backlog)) [[unlikely]] {
        return utility::unexpected(Failing());
    }
    return {};
}

RdmaResult<RdmaCommunication> RdmaListener::Accept() noexcept {
    while (true) {
        auto accepted = TryAccept();
        if (accepted) {
            return accepted;
        }
        if (accepted.error() != make_error_code(RdmaErrc::kNotReady)) [[unlikely]] {
            return utility::unexpected(accepted.error());
        }
    }
}

RdmaResult<RdmaCommunication> RdmaListener::TryAccept() noexcept {
    ::rdma_cm_event* event{nullptr};
    switch (PollCmEvent(event_channel_, 0, &event)) {
        case CmEventStatus::kNone:
            return utility::unexpected(make_error_code(RdmaErrc::kNotReady));
        case CmEventStatus::kFailure:
            return utility::unexpected(Failing());
        case CmEventStatus::kEvent:
            break;
    }

    if (event->event != RDMA_CM_EVENT_CONNECT_REQUEST || !event->id) [[unlikely]] {
        ::rdma_ack_cm_event(event);
        return utility::unexpected(make_error_code(RdmaErrc::kNotReady));
    }

    auto* cm_id = event->id;
    if (!resource_->Serves(*cm_id)) [[unlikely]] {
        ::rdma_reject(cm_id, nullptr, 0);
        ::rdma_ack_cm_event(event);
        return utility::unexpected(make_error_code(RdmaErrc::kDeviceMismatch));
    }

    ::rdma_event_channel* channel = ::rdma_create_event_channel();
    if (!channel) [[unlikely]] {
        ::rdma_reject(cm_id, nullptr, 0);
        ::rdma_ack_cm_event(event);
        return utility::unexpected(Failing());
    }
    if (::rdma_ack_cm_event(event) != 0) [[unlikely]] {
        ::rdma_destroy_event_channel(channel);
        ::rdma_reject(cm_id, nullptr, 0);
        return utility::unexpected(Failing());
    }
    if (::rdma_migrate_id(cm_id, channel) != 0) [[unlikely]] {
        ::rdma_destroy_event_channel(channel);
        ::rdma_reject(cm_id, nullptr, 0);
        return utility::unexpected(Failing());
    }

    try {
        return RdmaCommunication(cm_id, channel, *resource_, RdmaChannelOptions{}, /*passive=*/true);
    } catch (const std::exception&) {
        ::rdma_reject(cm_id, nullptr, 0);
        ::rdma_destroy_id(cm_id);
        ::rdma_destroy_event_channel(channel);
        return utility::unexpected(make_error_code(RdmaErrc::kOperationFailed));
    }
}

RdmaListener::Handle RdmaListener::event_channel_handle() const noexcept {
    return event_channel_ ? event_channel_->fd : -1;
}

std::uint16_t RdmaListener::BoundPort() const noexcept {
    // rdma_get_src_port answers in network byte order (__be16); the advertised
    // port has to be the host-order value a sockaddr would hold.
    return communication_id_ ? static_cast<std::uint16_t>(::ntohs(::rdma_get_src_port(communication_id_))) : 0;
}

RdmaListener::~RdmaListener() noexcept {
    if (communication_id_) {
        ::rdma_destroy_id(communication_id_);
        communication_id_ = nullptr;
    }
    if (event_channel_) {
        ::rdma_destroy_event_channel(event_channel_);
        event_channel_ = nullptr;
    }
}
}  // namespace nbio::net

#endif  // defined(NBIO_ENABLE_RDMA) && defined(__linux__)
