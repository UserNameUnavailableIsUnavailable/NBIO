#if defined(NBIO_ENABLE_RDMA) && defined(__linux__)

#include <poll.h>
#include <rdma/rdma_cma.h>

#include <cerrno>
#include <nbio/net/rdma_connector.hpp>
#include <stdexcept>
#include <system_error>
#include <utility>

namespace nbio::net {
namespace {
std::error_code Failing(const char* what) { return {errno, std::system_category()}; }

RdmaResult<::rdma_cm_event*> WaitCmEvent(::rdma_event_channel* channel, int timeout_ms) noexcept {
    pollfd waiter{.fd = channel->fd, .events = POLLIN, .revents = 0};
    if (::poll(&waiter, 1, timeout_ms) <= 0) [[unlikely]] {
        return utility::unexpected(std::make_error_code(std::errc::timed_out));
    }
    ::rdma_cm_event* event{nullptr};
    if (::rdma_get_cm_event(channel, &event) != 0) [[unlikely]] {
        return utility::unexpected(Failing("Failed to get an rdma CM event"));
    }
    return event;
}
}  // namespace

RdmaConnector::RdmaConnector(RdmaResource& resource) : resource_(&resource) {
    event_channel_ = ::rdma_create_event_channel();
    if (!event_channel_) [[unlikely]] {
        throw std::system_error(Failing("Failed to create an rdma event channel"),
                                "Failed to create an rdma event channel");
    }
    if (::rdma_create_id(event_channel_, &communication_id_, nullptr, RDMA_PS_TCP) != 0) [[unlikely]] {
        const auto why = Failing("Failed to create an rdma id");
        ::rdma_destroy_event_channel(event_channel_);
        event_channel_ = nullptr;
        throw std::system_error(why, "Failed to create an rdma id");
    }
}

RdmaConnector::RdmaConnector(RdmaConnector&& other) noexcept { swap(*this, other); }

RdmaConnector& RdmaConnector::operator=(RdmaConnector&& other) noexcept {
    if (this != &other) {
        Release();
        swap(*this, other);
    }
    return *this;
}

void swap(RdmaConnector& lhs, RdmaConnector& rhs) noexcept {
    using std::swap;
    swap(lhs.communication_id_, rhs.communication_id_);
    swap(lhs.event_channel_, rhs.event_channel_);
    swap(lhs.resource_, rhs.resource_);
    swap(lhs.phase_, rhs.phase_);
}

void RdmaConnector::Release() noexcept {
    if (communication_id_) {
        ::rdma_destroy_id(communication_id_);
        communication_id_ = nullptr;
    }
    if (event_channel_) {
        ::rdma_destroy_event_channel(event_channel_);
        event_channel_ = nullptr;
    }
    phase_ = 0;
}

RdmaConnector::~RdmaConnector() noexcept { Release(); }

int RdmaConnector::event_channel_handle() const noexcept { return event_channel_ ? event_channel_->fd : -1; }

RdmaResult<void> RdmaConnector::Bind(const net::Address& source) noexcept {
    if (::rdma_bind_addr(communication_id_, const_cast<::sockaddr*>(source.storage<::sockaddr>())) != 0) [[unlikely]] {
        return utility::unexpected(Failing("Failed to bind the rdma address"));
    }
    return {};
}

RdmaResult<void> RdmaConnector::SubmitResolve(const net::Address& target) noexcept {
    if (communication_id_ == nullptr) [[unlikely]] {
        return utility::unexpected(make_error_code(RdmaErrc::kInvalidState));
    }
    // rdma_resolve_addr returns immediately; the resolution finishes by emitting
    // RDMA_CM_EVENT_ADDR_RESOLVED on the event channel, which is awaited afterwards.
    
    if (::rdma_resolve_addr(communication_id_, nullptr, const_cast<::sockaddr*>(target.storage<::sockaddr>()),
                            address_resolve_timeout_ms_) != 0) [[unlikely]] {
        return utility::unexpected(Failing("Failed to resolve the rdma address"));
    }
    phase_ = 1;
    return {};
}

RdmaResult<void> RdmaConnector::AdvanceResolve(int poll_timeout_ms) noexcept {
    if (communication_id_ == nullptr) [[unlikely]] {
        return utility::unexpected(make_error_code(RdmaErrc::kInvalidState));
    }
    if (phase_ != 1 && phase_ != 2) [[unlikely]] {
        return utility::unexpected(make_error_code(RdmaErrc::kInvalidState));
    }

    auto event = WaitCmEvent(event_channel_, poll_timeout_ms);
    if (!event) {
        if (event.error() == std::make_error_code(std::errc::timed_out)) {
            return utility::unexpected(make_error_code(RdmaErrc::kNotReady));
        }
        return utility::unexpected(event.error());
    }
    const auto type = (*event)->event;
    ::rdma_ack_cm_event(*event);

    if (phase_ == 1) {
        if (type != RDMA_CM_EVENT_ADDR_RESOLVED) [[unlikely]] {
            return utility::unexpected(make_error_code(RdmaErrc::kUnexpectedEvent));
        }
        if (!resource_->Serves(*communication_id_)) [[unlikely]] {
            return utility::unexpected(make_error_code(RdmaErrc::kDeviceMismatch));
        }
        // The route can only be resolved once the address has been; submit it and
        // wait for ROUTE_RESOLVED on the next step.
        if (::rdma_resolve_route(communication_id_, route_resolve_timeout_ms_) != 0) [[unlikely]] {
            return utility::unexpected(Failing("Failed to resolve the rdma route"));
        }
        phase_ = 2;
        return {};
    }

    if (type != RDMA_CM_EVENT_ROUTE_RESOLVED) [[unlikely]] {
        return utility::unexpected(make_error_code(RdmaErrc::kUnexpectedEvent));
    }
    phase_ = 3;
    return {};
}

RdmaResult<RdmaCommunication> RdmaConnector::CompleteResolve() noexcept {
    if (phase_ != 3) [[unlikely]] {
        return utility::unexpected(make_error_code(RdmaErrc::kNotReady));
    }
    // The route is known; build the queue pair and hand everything over.
    try {
        RdmaCommunication communication(communication_id_, event_channel_, *resource_, RdmaChannelOptions{},
                                        /*passive=*/false);
        communication_id_ = nullptr;
        event_channel_ = nullptr;
        phase_ = 0;
        return RdmaCommunication(std::move(communication));
    } catch (const std::exception&) {
        return utility::unexpected(make_error_code(RdmaErrc::kOperationFailed));
    }
}

RdmaResult<RdmaCommunication> RdmaConnector::Connect(const net::Address& target) noexcept {
    if (auto submitted = SubmitResolve(target); !submitted) [[unlikely]] {
        return utility::unexpected(submitted.error());
    }
    if (auto addressed = AdvanceResolve(address_resolve_timeout_ms_); !addressed) [[unlikely]] {
        return utility::unexpected(addressed.error());
    }
    if (auto routed = AdvanceResolve(route_resolve_timeout_ms_); !routed) [[unlikely]] {
        return utility::unexpected(routed.error());
    }
    return CompleteResolve();
}
}  // namespace nbio::net

#endif  // defined(NBIO_ENABLE_RDMA) && defined(__linux__)
