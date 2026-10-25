#if defined(NBIO_ENABLE_RDMA) && defined(__linux__)
#include <fcntl.h>
#include <poll.h>
#include <rdma/rdma_cma.h>

#include <cerrno>
#include <nbio/net/address.hpp>
#include <nbio/net/rdma_listener.hpp>
#include <stdexcept>
#include <utility>

namespace nbio::net {
namespace {
std::error_code Failing() { return {errno, std::system_category()}; }

enum class CmEventStatus {
    kEvent,
    kNone,
    kFailure,
};

// Takes one event, waiting up to `timeout_ms` for one to exist. Polling first is
// what lets a blocking fd be used by a caller that asked not to wait, and what
// lets a channel drive the same fd through the multiplexer.
CmEventStatus PollCmEvent(::rdma_event_channel* channel, int timeout_ms, ::rdma_cm_event** event) noexcept {
    pollfd waiter{.fd = channel->fd, .events = POLLIN, .revents = 0};
    const int ready = ::poll(&waiter, 1, timeout_ms);
    if (ready == 0) {
        return CmEventStatus::kNone;
    }
    if (ready < 0) {
        return CmEventStatus::kFailure;
    }
    if (::rdma_get_cm_event(channel, event) == 0) {
        return CmEventStatus::kEvent;
    }
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
        return CmEventStatus::kNone;
    }
    return CmEventStatus::kFailure;
}
}  // namespace

RdmaListener::RdmaListener(RdmaResource& resource) : resource_(&resource) {
    // The id is created on the event channel, so that has to exist first. Nothing
    // else is created here: what a connection is built from comes from the manager,
    // and it is only needed once one asks to be admitted.
    event_channel_ = ::rdma_create_event_channel();
    if (!event_channel_) [[unlikely]] {
        throw std::runtime_error("Failed to create rdma event channel");
    }
    if (::rdma_create_id(event_channel_, &communication_id_, nullptr, RDMA_PS_TCP)) [[unlikely]] {
        ::rdma_destroy_event_channel(event_channel_);
        event_channel_ = nullptr;
        throw std::runtime_error("Failed to create rdma ID");
    }
}

RdmaResult<void> RdmaListener::Listen(net::Address address, int backlog) noexcept {
    address_ = std::move(address);
    if (::rdma_bind_addr(communication_id_, address_.storage<::sockaddr>())) [[unlikely]] {
        return utility::unexpected(Failing());
    }
    // The address is what picks the device, and the manager holds one: an address
    // that resolves somewhere else would give every connection it admits a queue
    // pair built with regions and keys that device does not know.
    if (!resource_->Serves(*communication_id_)) [[unlikely]] {
        return utility::unexpected(make_error_code(RdmaErrc::kDeviceMismatch));
    }
    if (::rdma_listen(communication_id_, backlog)) [[unlikely]] {
        return utility::unexpected(Failing());
    }
    return {};
}

RdmaResult<RdmaConnector> RdmaListener::Accept() noexcept {
    // A blocking listener waits for the next event, like accept(2) on a blocking
    // socket; a non-blocking one takes what is there and says so if nothing is.
    const int timeout_ms = non_blocking_ ? 0 : -1;
    while (true) {
        ::rdma_cm_event* event{nullptr};
        switch (PollCmEvent(event_channel_, timeout_ms, &event)) {
            case CmEventStatus::kNone:
                return utility::unexpected(make_error_code(RdmaErrc::kNotReady));
            case CmEventStatus::kFailure:
                return utility::unexpected(Failing());
            case CmEventStatus::kEvent:
                break;
        }

        if (event->event != RDMA_CM_EVENT_CONNECT_REQUEST || !event->id) [[unlikely]] {
            // The listener's channel carries what happens to the listener -- a
            // device going away, the listener being taken down -- and nothing of it
            // is a connection to admit. A connection's own events are on the
            // channel it was migrated to, so none of them can turn up here.
            ::rdma_ack_cm_event(event);
            continue;
        }

        auto* cm_id = event->id;  // the kernel makes one id per connection
        if (!resource_->Serves(*cm_id)) [[unlikely]] {
            ::rdma_reject(cm_id, nullptr, 0);
            ::rdma_ack_cm_event(event);
            return utility::unexpected(make_error_code(RdmaErrc::kDeviceMismatch));
        }

        // An listener gets a new connection request on the event channel, then it creates a new channel for that
        // connection, moving the connector's id to that event channel.
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
        // move the cm_id to the connector's channel for load balancing
        if (::rdma_migrate_id(cm_id, channel) != 0) [[unlikely]] {
            ::rdma_destroy_event_channel(channel);
            ::rdma_reject(cm_id, nullptr, 0);
            return utility::unexpected(Failing());
        }

        try {
            // The queue pair is built and the id is the connection's, but nothing is
            // accepted yet: the caller posts its receives first and then calls
            // Establish(), which is what lets the peer send.
            RdmaConnector connector(cm_id, channel, *resource_);
            return RdmaConnector(std::move(connector));
        } catch (const std::exception& error) {
            // The connection never took either of them.
            ::rdma_reject(cm_id, nullptr, 0);
            ::rdma_destroy_id(cm_id);
            ::rdma_destroy_event_channel(channel);
            return utility::unexpected(make_error_code(RdmaErrc::kOperationFailed));
        }
    }
}

RdmaResult<void> RdmaListener::ReuseAddress(bool enabled) noexcept {
    int reuse = enabled ? 1 : 0;
    auto ret = ::rdma_set_option(communication_id_, RDMA_OPTION_ID, RDMA_OPTION_ID_REUSEADDR, &reuse, sizeof(reuse));
    if (ret != 0) {
        return utility::unexpected(Failing());
    }
    return {};
}

RdmaListener::~RdmaListener() noexcept {
    // Connections borrow the manager's device, domain and chunks, so they are all
    // gone before it is: this listener gives back its own id and its own channel.
    if (communication_id_) {
        ::rdma_destroy_id(communication_id_);
    }
    if (event_channel_) {
        ::rdma_destroy_event_channel(event_channel_);
    }
}
}  // namespace nbio::net

#endif  // defined(NBIO_ENABLE_RDMA) && defined(__linux__)
