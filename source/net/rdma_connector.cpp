#if defined(NBIO_ENABLE_RDMA) && defined(__linux__)

#include <fcntl.h>
#include <infiniband/verbs.h>
#include <poll.h>
#include <rdma/rdma_cma.h>
#include <unistd.h>

#include <algorithm>
#include <cassert>
#include <cerrno>
#include <cstdint>
#include <limits>
#include <nbio/net/rdma_connector.hpp>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>

namespace nbio::net {
namespace {
std::error_code Failing([[maybe_unused]] const char* what) { return {errno, std::system_category()}; }

std::string DescribeCmEvent(const ::rdma_cm_event& event) {
    std::string description = ::rdma_event_str(event.event);
    if (event.status != 0) {
        description += " (status=" + std::to_string(event.status) + ")";
    }
    return description;
}

// Waits for one event on a connection's own channel. The channels a connection
// owns are non-blocking, because an event loop may be watching them, so this polls
// the fd and then takes the event rather than blocking inside rdma_get_cm_event.
// The caller acknowledges what comes back.
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

// Whether `chunk` lies entirely inside `region`. This is the whole of the check a
// post needs: the descriptor carries the region's key, so a span inside the region
// needs no further proof that it may be used.
bool Contains(std::span<const char> region, std::span<const char> chunk) noexcept {
    if (chunk.empty() || region.empty()) {
        return false;
    }
    const auto begin = reinterpret_cast<std::uintptr_t>(region.data());
    const auto end = begin + region.size();
    const auto first = reinterpret_cast<std::uintptr_t>(chunk.data());
    if (first < begin || first > end) {
        return false;
    }
    return chunk.size() <= end - first;
}
}  // namespace

void RdmaConnector::Ring::Reset(std::size_t capacity) {
    slots_.assign(capacity == 0 ? 1 : capacity, Slot{});
    head_ = 1;
    next_seq_ = 1;
    last_reaped_ = 0;
}

void RdmaConnector::Ring::AdvanceHead() noexcept {
    while (head_ < next_seq_) {
        const Slot& slot = At(head_);
        // A slot that is still in flight must not be freed: its completion would
        // land on whatever was posted into it next.
        if (!slot.dropped || slot.in_flight) {
            break;
        }
        if (slot.seq != head_) {
            break;
        }
        At(head_) = Slot{};
        ++head_;
    }
}

RdmaConnector::RdmaConnector(::rdma_cm_id* communication_id, ::rdma_event_channel* event_channel,
                             RdmaResource& resource, RdmaChannelOptions options)
    : communication_id_(communication_id), event_channel_(event_channel), resource_(&resource), options_(options) {
    if (!communication_id_ || !event_channel_) {
        throw std::invalid_argument("an rdma connection needs a communication id and a channel of its own");
    }
    // A connection the kernel has already resolved for us: the id belongs to a
    // request, so there is nothing to resolve and no destination to reach. The queue
    // pair is built here and not accepted: the caller posts its receives first, and
    // Establish() is what lets the peer send.
    passive_ = true;
    send_ring_.Reset(options_.send_queue_depth);
    receive_ring_.Reset(options_.receive_queue_depth);
    BuildQueuePair();
}

RdmaConnector::RdmaConnector(RdmaResource& resource, RdmaChannelOptions options)
    : resource_(&resource), options_(options) {
    // The channel comes first, because the id is created on it, and it is the
    // channel this connection's own events -- established, rejected, disconnected --
    // arrive on for the rest of its life.
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
    send_ring_.Reset(options_.send_queue_depth);
    receive_ring_.Reset(options_.receive_queue_depth);
}

RdmaResult<void> RdmaConnector::BindSource(const net::Address& local) noexcept {
    if (passive_) [[unlikely]] {
        // A passive connection was bound by the listener that admitted it.
        return utility::unexpected(make_error_code(RdmaErrc::kInvalidState));
    }
    if (::rdma_bind_addr(communication_id_, const_cast<::sockaddr*>(local.storage<::sockaddr>())) != 0) [[unlikely]] {
        return utility::unexpected(Failing("Failed to bind the rdma address"));
    }
    return {};
}

void RdmaConnector::BuildQueuePair() {
    try {
        // Each direction gets its own completion channel, so that a wait in one
        // direction can never be woken by -- or have to drain -- a completion in
        // the other.
        send_completion_channel_ = ::ibv_create_comp_channel(communication_id_->verbs);
        if (!send_completion_channel_) {
            throw std::runtime_error("Failed to create the rdma send completion channel");
        }
        receive_completion_channel_ = ::ibv_create_comp_channel(communication_id_->verbs);
        if (!receive_completion_channel_) {
            throw std::runtime_error("Failed to create the rdma receive completion channel");
        }
        // Draining a queue reads events until ibv_get_cq_event reports EAGAIN, so
        // the fds have to be non-blocking or that loop never returns -- and a driver
        // that hands an event to a blocking reader would stop the event loop instead
        // of waking it.
        for (auto* channel : {send_completion_channel_, receive_completion_channel_}) {
            const int flags = ::fcntl(channel->fd, F_GETFL, 0);
            if (flags < 0 || ::fcntl(channel->fd, F_SETFL, flags | O_NONBLOCK) < 0) {
                throw std::runtime_error("Failed to make an rdma completion channel non-blocking");
            }
        }

        send_completion_queue_ = ::ibv_create_cq(communication_id_->verbs, static_cast<int>(options_.send_queue_depth),
                                                 nullptr, send_completion_channel_, 0);
        if (!send_completion_queue_) {
            throw std::runtime_error("Failed to create the rdma send completion queue");
        }
        receive_completion_queue_ = ::ibv_create_cq(
            communication_id_->verbs, static_cast<int>(options_.receive_queue_depth), nullptr, receive_completion_channel_, 0);
        if (!receive_completion_queue_) {
            throw std::runtime_error("Failed to create the rdma receive completion queue");
        }

        ibv_qp_init_attr attributes{
            .qp_context = nullptr,
            .send_cq = send_completion_queue_,
            .recv_cq = receive_completion_queue_,
            .srq = nullptr,
            .cap =
                {
                    .max_send_wr = options_.send_queue_depth,
                    .max_recv_wr = options_.receive_queue_depth,
                    .max_send_sge = options_.max_send_sge,
                    .max_recv_sge = options_.max_receive_sge,
                    .max_inline_data = options_.inline_threshold,
                },
            .qp_type = IBV_QPT_RC,
            .sq_sig_all = 0,
        };
        if (::rdma_create_qp(communication_id_, resource_->protection_domain(), &attributes) != 0) {
            throw std::runtime_error("Failed to create the rdma queue pair");
        }
        queue_pair_ = communication_id_->qp;

        // A stream whose queues are not armed can never be waited on, and the
        // constructor is the one place allowed to throw, so the failure unwinds
        // here instead of being carried as state.
        if (auto armed = Arm(send_completion_queue_); !armed) [[unlikely]] {
            throw std::system_error(armed.error(), "Failed to arm the rdma send completion queue");
        }
        if (auto armed = Arm(receive_completion_queue_); !armed) [[unlikely]] {
            throw std::system_error(armed.error(), "Failed to arm the rdma receive completion queue");
        }
    } catch (...) {
        // The id and the channel stay: the listener rejects the connection with the
        // one and this object's destructor gives back the other.
        ReleaseDeviceObjects();
        throw;
    }
}

void RdmaConnector::ReleaseDeviceObjects() noexcept {
    if (queue_pair_) {
        ::rdma_destroy_qp(communication_id_);
        queue_pair_ = nullptr;
    }
    if (send_completion_queue_) {
        ::ibv_destroy_cq(send_completion_queue_);
        send_completion_queue_ = nullptr;
    }
    if (receive_completion_queue_) {
        ::ibv_destroy_cq(receive_completion_queue_);
        receive_completion_queue_ = nullptr;
    }
    if (send_completion_channel_) {
        ::ibv_destroy_comp_channel(send_completion_channel_);
        send_completion_channel_ = nullptr;
    }
    if (receive_completion_channel_) {
        ::ibv_destroy_comp_channel(receive_completion_channel_);
        receive_completion_channel_ = nullptr;
    }
}

RdmaConnector::RdmaConnector(RdmaConnector&& other) noexcept { swap(*this, other); }

RdmaConnector& RdmaConnector::operator=(RdmaConnector&& other) noexcept {
    if (this != &other) {
        Reset();
        swap(*this, other);
    }
    return *this;
}

void swap(RdmaConnector& lhs, RdmaConnector& rhs) noexcept {
    using std::swap;
    swap(lhs.communication_id_, rhs.communication_id_);
    swap(lhs.event_channel_, rhs.event_channel_);
    swap(lhs.send_completion_channel_, rhs.send_completion_channel_);
    swap(lhs.receive_completion_channel_, rhs.receive_completion_channel_);
    swap(lhs.send_completion_queue_, rhs.send_completion_queue_);
    swap(lhs.receive_completion_queue_, rhs.receive_completion_queue_);
    swap(lhs.queue_pair_, rhs.queue_pair_);
    swap(lhs.resource_, rhs.resource_);
    swap(lhs.options_, rhs.options_);
    swap(lhs.send_ring_, rhs.send_ring_);
    swap(lhs.receive_ring_, rhs.receive_ring_);
    swap(lhs.error_, rhs.error_);
    swap(lhs.peer_closed_, rhs.peer_closed_);
    swap(lhs.passive_, rhs.passive_);
    swap(lhs.established_, rhs.established_);
}

RdmaConnector::~RdmaConnector() noexcept { Reset(); }

void RdmaConnector::Close() noexcept { Reset(); }

void RdmaConnector::Reset() noexcept {
    // A connection that was prepared but never established still holds a request the
    // peer is waiting on. Rejecting is how this end says no, rather than leaving the
    // peer to time out.
    if (communication_id_ && !established_) {
        if (passive_ && queue_pair_) {
            ::rdma_reject(communication_id_, nullptr, 0);
        }
    } else if (communication_id_ && queue_pair_) {
        // A disconnect first, so the peer is told the connection is over rather than
        // left with a queue pair that went quiet. Its disconnect event is the peer's
        // to read on its own channel.
        ::rdma_disconnect(communication_id_);
    }
    // Destroying the queue pair discards outstanding work requests, so it has to
    // happen before their bookkeeping goes.
    if (queue_pair_) {
        ::rdma_destroy_qp(communication_id_);
        queue_pair_ = nullptr;
    }
    // Empty both queues before destroying them. A provider will not finish tearing a
    // completion queue down while it still has completions or unacknowledged events
    // outstanding -- siw waits inside ibv_destroy_cq for them -- so a link closed
    // right after its last completion would hang here instead of returning.
    DrainCompletionQueue(send_completion_queue_);
    DrainCompletionQueue(receive_completion_queue_);
    if (receive_completion_queue_) {
        ::ibv_destroy_cq(receive_completion_queue_);
        receive_completion_queue_ = nullptr;
    }
    if (send_completion_queue_) {
        ::ibv_destroy_cq(send_completion_queue_);
        send_completion_queue_ = nullptr;
    }
    if (receive_completion_channel_) {
        ::ibv_destroy_comp_channel(receive_completion_channel_);
        receive_completion_channel_ = nullptr;
    }
    if (send_completion_channel_) {
        ::ibv_destroy_comp_channel(send_completion_channel_);
        send_completion_channel_ = nullptr;
    }
    if (communication_id_) {
        ::rdma_destroy_id(communication_id_);
        communication_id_ = nullptr;
    }
    if (event_channel_) {
        ::rdma_destroy_event_channel(event_channel_);
        event_channel_ = nullptr;
    }
    // Every buffer named by a request is the caller's again: the queue pair is
    // gone, so nothing will ever complete or touch them.
    send_ring_.Reset(options_.send_queue_depth);
    receive_ring_.Reset(options_.receive_queue_depth);
    peer_closed_ = true;
}

void RdmaConnector::DrainCompletionQueue(::ibv_cq* queue) noexcept {
    if (!queue) {
        return;
    }
    ibv_wc completions[16];
    while (::ibv_poll_cq(queue, 16, completions) > 0) {
    }
    // Events are acknowledged where they are consumed, so whatever is left is
    // acknowledged here; a queue with unacknowledged events refuses to be destroyed.
    ::ibv_comp_channel* channel =
        queue == send_completion_queue_ ? send_completion_channel_ : receive_completion_channel_;
    if (channel) {
        ::ibv_cq* notified{nullptr};
        void* context{nullptr};
        unsigned count = 0;
        while (::ibv_get_cq_event(channel, &notified, &context) == 0) {
            ++count;
        }
        if (count != 0) {
            ::ibv_ack_cq_events(queue, count);
        }
    }
}

RdmaResult<void> RdmaConnector::Arm(::ibv_cq* queue) noexcept {
    if (queue && ::ibv_req_notify_cq(queue, 0) != 0) [[unlikely]] {
        return utility::unexpected(Failing("Failed to arm an rdma completion queue"));
    }
    return {};
}

void RdmaConnector::DrainEvents(::ibv_comp_channel* channel, ::ibv_cq* queue) noexcept {
    if (!channel || !queue) {
        return;
    }
    ::ibv_cq* notified{nullptr};
    void* context{nullptr};
    unsigned count = 0;
    while (::ibv_get_cq_event(channel, &notified, &context) == 0) {
        ++count;
    }
    if (count != 0) {
        ::ibv_ack_cq_events(queue, count);
    }
}

void RdmaConnector::HandleCompletion(Ring& ring, const ::ibv_wc& completion) noexcept {
    const RdmaSeq seq = completion.wr_id;
    if (!ring.Live(seq)) {
        return;
    }
    Slot& slot = ring.At(seq);
    if (slot.seq != seq) {
        return;
    }
    slot.in_flight = false;
    slot.bytes = completion.byte_len;
    if (completion.status != IBV_WC_SUCCESS) [[unlikely]] {
        if (completion.status == IBV_WC_WR_FLUSH_ERR) {
            // The queue pair is being torn down or has gone to error: every request
            // still posted is flushed, and none of them failed on its own account.
            peer_closed_ = true;
            slot.status = make_error_code(RdmaErrc::kPeerClosed);
        } else {
            Fail(make_error_code(RdmaErrc::kCompletionFailed));
            slot.status = error_;
        }
    }
    if (!slot.dropped) {
        slot.completed = true;
    }
    if (seq > ring.last_reaped_) {
        ring.last_reaped_ = seq;
    }
    ring.AdvanceHead();
}

RdmaResult<std::size_t> RdmaConnector::ReapSend() noexcept {
    if (!send_completion_queue_) {
        return std::size_t{0};
    }
    std::size_t reaped = 0;
    ibv_wc completions[16];
    while (true) {
        const int count = ::ibv_poll_cq(send_completion_queue_, 16, completions);
        if (count <= 0) {
            break;
        }
        for (int i = 0; i < count; ++i) {
            HandleCompletion(send_ring_, completions[i]);
        }
        reaped += static_cast<std::size_t>(count);
    }
    return reaped;
}

RdmaResult<std::size_t> RdmaConnector::ReapReceive() noexcept {
    if (!receive_completion_queue_) {
        return std::size_t{0};
    }
    std::size_t reaped = 0;
    ibv_wc completions[16];
    while (true) {
        const int count = ::ibv_poll_cq(receive_completion_queue_, 16, completions);
        if (count <= 0) {
            break;
        }
        for (int i = 0; i < count; ++i) {
            HandleCompletion(receive_ring_, completions[i]);
        }
        reaped += static_cast<std::size_t>(count);
    }
    return reaped;
}

RdmaResult<RdmaSeq> RdmaConnector::PostSend(std::span<const char> buffer) noexcept {
    if (failed()) [[unlikely]] {
        return utility::unexpected(error_);
    }
    if (queue_pair_ == nullptr) [[unlikely]] {
        return utility::unexpected(make_error_code(RdmaErrc::kInvalidState));
    }
    if (buffer.empty() || buffer.size() > std::numeric_limits<std::uint32_t>::max() ||
        !Contains(resource_->send_memory_region(), buffer)) [[unlikely]] {
        return utility::unexpected(make_error_code(RdmaErrc::kInvalidBuffer));
    }
    if (!send_ring_.has_room()) [[unlikely]] {
        return utility::unexpected(make_error_code(RdmaErrc::kQueueFull));
    }

    const RdmaSeq seq = send_ring_.Next();
    Slot& slot = send_ring_.At(seq);
    slot = Slot{.seq = seq,
                .address = const_cast<char*>(buffer.data()),
                .length = static_cast<std::uint32_t>(buffer.size()),
                .in_flight = true};

    ibv_sge segment{.addr = reinterpret_cast<std::uintptr_t>(buffer.data()),
                    .length = static_cast<std::uint32_t>(buffer.size()),
                    .lkey = resource_->send_lkey()};
    ibv_send_wr request{};
    request.wr_id = seq;
    request.sg_list = &segment;
    request.num_sge = 1;
    request.opcode = IBV_WR_SEND;
    request.send_flags = IBV_SEND_SIGNALED;

    ibv_send_wr* rejected{nullptr};
    if (const int code = ::ibv_post_send(queue_pair_, &request, &rejected); code != 0) [[unlikely]] {
        // The ring did not consume the number, so the slot goes back untouched.
        slot = Slot{};
        return utility::unexpected(std::error_code{code, std::generic_category()});
    }
    send_ring_.Advance();
    return seq;
}

RdmaResult<RdmaSeq> RdmaConnector::PostReceive(std::span<char> buffer) noexcept {
    if (failed()) [[unlikely]] {
        return utility::unexpected(error_);
    }
    if (queue_pair_ == nullptr) [[unlikely]] {
        return utility::unexpected(make_error_code(RdmaErrc::kInvalidState));
    }
    if (buffer.empty() || buffer.size() > std::numeric_limits<std::uint32_t>::max() ||
        !Contains(resource_->receive_memory_region(), buffer)) [[unlikely]] {
        return utility::unexpected(make_error_code(RdmaErrc::kInvalidBuffer));
    }
    if (!receive_ring_.has_room()) [[unlikely]] {
        return utility::unexpected(make_error_code(RdmaErrc::kQueueFull));
    }

    const RdmaSeq seq = receive_ring_.Next();
    Slot& slot = receive_ring_.At(seq);
    slot = Slot{.seq = seq,
                .address = buffer.data(),
                .length = static_cast<std::uint32_t>(buffer.size()),
                .in_flight = true};

    ibv_sge segment{.addr = reinterpret_cast<std::uintptr_t>(buffer.data()),
                    .length = static_cast<std::uint32_t>(buffer.size()),
                    .lkey = resource_->receive_lkey()};
    ibv_recv_wr request{};
    request.wr_id = seq;
    request.sg_list = &segment;
    request.num_sge = 1;

    ibv_recv_wr* rejected{nullptr};
    if (const int code = ::ibv_post_recv(queue_pair_, &request, &rejected); code != 0) [[unlikely]] {
        slot = Slot{};
        return utility::unexpected(std::error_code{code, std::generic_category()});
    }
    receive_ring_.Advance();
    return seq;
}

RdmaResult<RdmaSeq> RdmaConnector::WaitOn(Ring& ring, ::ibv_cq* queue, ::ibv_comp_channel* channel, RdmaSeq seq,
                                          int timeout_ms) noexcept {
    if (seq == 0 || seq >= ring.Next()) [[unlikely]] {
        return utility::unexpected(make_error_code(RdmaErrc::kInvalidSequence));
    }
    // Completions that have already been reaped are answered first: a stream that
    // failed later must not hide the results that arrived before it did.
    if (ring.last_reaped_ >= seq) [[likely]] {
        return ring.last_reaped_;
    }
    if (failed()) [[unlikely]] {
        return utility::unexpected(error_);
    }
    if (queue == nullptr || channel == nullptr) [[unlikely]] {
        return utility::unexpected(make_error_code(RdmaErrc::kInvalidState));
    }

    // Arm before looking: a completion that lands between the look and the poll
    // would raise its event before anyone asked for one, and the wait would sleep
    // through it.
    if (auto armed = Arm(queue); !armed) [[unlikely]] {
        return utility::unexpected(armed.error());
    }
    while (true) {
        if (queue == send_completion_queue_) {
            (void)ReapSend();
        } else {
            (void)ReapReceive();
        }
        if (ring.last_reaped_ >= seq) {
            return ring.last_reaped_;
        }
        if (failed()) [[unlikely]] {
            return utility::unexpected(error_);
        }
        if (timeout_ms == 0) {
            return ring.last_reaped_;
        }

        pollfd waiter{.fd = channel->fd, .events = POLLIN, .revents = 0};
        if (::poll(&waiter, 1, timeout_ms) <= 0) {
            // A deadline that expired is not a failure: the mark says how far the
            // device got, and the caller compares it with what it asked for.
            return ring.last_reaped_;
        }
        DrainEvents(channel, queue);
        if (auto armed = Arm(queue); !armed) [[unlikely]] {
            return utility::unexpected(armed.error());
        }
    }
}

RdmaResult<RdmaSeq> RdmaConnector::WaitSend(RdmaSeq seq, int timeout_ms) noexcept {
    return WaitOn(send_ring_, send_completion_queue_, send_completion_channel_, seq, timeout_ms);
}

RdmaResult<RdmaSeq> RdmaConnector::WaitReceive(RdmaSeq seq, int timeout_ms) noexcept {
    return WaitOn(receive_ring_, receive_completion_queue_, receive_completion_channel_, seq, timeout_ms);
}

RdmaResult<RdmaSendResult> RdmaConnector::FetchSend(RdmaSeq seq) noexcept {
    if (seq == 0 || seq >= send_ring_.Next()) [[unlikely]] {
        return utility::unexpected(make_error_code(RdmaErrc::kInvalidSequence));
    }
    const Slot& slot = send_ring_.At(seq);
    if (slot.seq != seq || slot.dropped) [[unlikely]] {
        return utility::unexpected(make_error_code(RdmaErrc::kInvalidSequence));
    }
    if (!slot.completed) [[unlikely]] {
        return utility::unexpected(make_error_code(RdmaErrc::kNotReady));
    }
    if (slot.status) [[unlikely]] {
        return utility::unexpected(slot.status);
    }
    return RdmaSendResult{.seq = seq, .buffer = std::span<char>(slot.address, slot.length)};
}

RdmaResult<RdmaReceiveResult> RdmaConnector::FetchReceive(RdmaSeq seq) noexcept {
    if (seq == 0 || seq >= receive_ring_.Next()) [[unlikely]] {
        return utility::unexpected(make_error_code(RdmaErrc::kInvalidSequence));
    }
    const Slot& slot = receive_ring_.At(seq);
    if (slot.seq != seq || slot.dropped) [[unlikely]] {
        return utility::unexpected(make_error_code(RdmaErrc::kInvalidSequence));
    }
    if (!slot.completed) [[unlikely]] {
        return utility::unexpected(make_error_code(RdmaErrc::kNotReady));
    }
    if (slot.status) [[unlikely]] {
        return utility::unexpected(slot.status);
    }
    return RdmaReceiveResult{.seq = seq,
                             .buffer = std::span<char>(slot.address, slot.length),
                             .bytes = slot.bytes};
}

void RdmaConnector::DropSend(RdmaSeq seq) noexcept {
    if (seq == 0 || seq >= send_ring_.Next()) {
        return;
    }
    Slot& slot = send_ring_.At(seq);
    if (slot.seq != seq) {
        return;
    }
    slot.dropped = true;
    send_ring_.AdvanceHead();
}

void RdmaConnector::DropReceive(RdmaSeq seq) noexcept {
    if (seq == 0 || seq >= receive_ring_.Next()) {
        return;
    }
    Slot& slot = receive_ring_.At(seq);
    if (slot.seq != seq) {
        return;
    }
    slot.dropped = true;
    receive_ring_.AdvanceHead();
}

void RdmaConnector::Fail(std::error_code error) noexcept {
    // The first failure is the cause and the rest are its consequences, so only
    // the first one is kept.
    if (!error_) {
        error_ = error;
    }
}

RdmaResult<void> RdmaConnector::SubmitEstablish() noexcept {
    if (submitted_) {
        return {};
    }
    ::rdma_conn_param param{};
    param.responder_resources = 1;
    param.initiator_depth = 1;
    param.retry_count = 7;
    param.rnr_retry_count = 7;  // stall rather than fail when receives run dry
    const int code = passive_ ? ::rdma_accept(communication_id_, &param) : ::rdma_connect(communication_id_, &param);
    if (code != 0) [[unlikely]] {
        return utility::unexpected(Failing("Failed to establish the rdma connection"));
    }
    submitted_ = true;
    return {};
}

RdmaResult<void> RdmaConnector::WaitForEstablish() noexcept {
    while (true) {
        auto event = WaitCmEvent(event_channel_, -1);
        if (!event) [[unlikely]] {
            return utility::unexpected(event.error());
        }
        const auto type = (*event)->event;
        const auto description = DescribeCmEvent(*(*event));
        ::rdma_ack_cm_event(*event);
        if (type == RDMA_CM_EVENT_ESTABLISHED) {
            established_ = true;
            return {};
        }
        if (type == RDMA_CM_EVENT_REJECTED || type == RDMA_CM_EVENT_UNREACHABLE ||
            type == RDMA_CM_EVENT_CONNECT_ERROR || type == RDMA_CM_EVENT_ADDR_ERROR ||
            type == RDMA_CM_EVENT_ROUTE_ERROR || type == RDMA_CM_EVENT_DEVICE_REMOVAL) [[unlikely]] {
            return utility::unexpected(make_error_code(RdmaErrc::kOperationFailed));
        }
        // Anything else is about the connection but is not its outcome; keep waiting.
    }
}

RdmaResult<void> RdmaConnector::AdvanceEstablish() noexcept {
    if (established_) {
        return {};
    }
    // The same rule the blocking form enforces: an active end without a target has
    // nothing to connect to, and a channel must not park waiting for one.
    if (!passive_ && queue_pair_ == nullptr) [[unlikely]] {
        return utility::unexpected(make_error_code(RdmaErrc::kNoTarget));
    }
    if (queue_pair_ == nullptr) [[unlikely]] {
        return utility::unexpected(make_error_code(RdmaErrc::kInvalidState));
    }
    if (auto submitted = SubmitEstablish(); !submitted) [[unlikely]] {
        return submitted;
    }

    // One event, and only if one is already there: a channel has parked on the fd,
    // so there is nothing for this thread to wait for.
    auto event = WaitCmEvent(event_channel_, 0);
    if (!event) {
        if (event.error() == std::make_error_code(std::errc::timed_out)) {
            return utility::unexpected(make_error_code(RdmaErrc::kNotReady));
        }
        return utility::unexpected(event.error());
    }
    const auto type = (*event)->event;
    const auto description = DescribeCmEvent(*(*event));
    ::rdma_ack_cm_event(*event);
    if (type == RDMA_CM_EVENT_ESTABLISHED) {
        established_ = true;
        return {};
    }
    if (type == RDMA_CM_EVENT_REJECTED || type == RDMA_CM_EVENT_UNREACHABLE ||
        type == RDMA_CM_EVENT_CONNECT_ERROR || type == RDMA_CM_EVENT_ADDR_ERROR ||
        type == RDMA_CM_EVENT_ROUTE_ERROR || type == RDMA_CM_EVENT_DEVICE_REMOVAL) [[unlikely]] {
        return utility::unexpected(make_error_code(RdmaErrc::kOperationFailed));
    }
    return utility::unexpected(make_error_code(RdmaErrc::kNotReady));
}

RdmaResult<void> RdmaConnector::BindTarget(const net::Address& peer) noexcept {
    if (failed()) [[unlikely]] {
        return utility::unexpected(error_);
    }
    if (passive_) [[unlikely]] {
        // A passive end has no destination to reach: the kernel already resolved it
        // when the request arrived, and the queue pair is built.
        return utility::unexpected(make_error_code(RdmaErrc::kInvalidState));
    }
    if (queue_pair_ != nullptr) [[unlikely]] {
        return utility::unexpected(make_error_code(RdmaErrc::kInvalidState));
    }

    const auto take_event = [this](::rdma_cm_event_type wanted) noexcept -> RdmaResult<void> {
        auto event = WaitCmEvent(event_channel_, 2000);
        if (!event) {
            return utility::unexpected(event.error());
        }
        const auto received = (*event)->event;
        const auto description = DescribeCmEvent(*(*event));
        ::rdma_ack_cm_event(*event);
        if (received != wanted) {
            return utility::unexpected(make_error_code(RdmaErrc::kUnexpectedEvent));
        }
        return {};
    };

    // Resolving the address is what gives the id its device context.
    if (::rdma_resolve_addr(communication_id_, nullptr, const_cast<::sockaddr*>(peer.storage<::sockaddr>()), 2000) != 0)
        [[unlikely]] {
        return utility::unexpected(Failing("Failed to resolve the rdma address"));
    }
    if (auto resolved = take_event(RDMA_CM_EVENT_ADDR_RESOLVED); !resolved) [[unlikely]] {
        return utility::unexpected(resolved.error());
    }

    // The address decided which device this connection runs on, so this is the
    // first point the resource can be checked against it: a connection built with
    // another device's regions and keys would fail every completion.
    if (!resource_->Serves(*communication_id_)) [[unlikely]] {
        return utility::unexpected(make_error_code(RdmaErrc::kDeviceMismatch));
    }

    if (::rdma_resolve_route(communication_id_, 2000) != 0) [[unlikely]] {
        return utility::unexpected(Failing("Failed to resolve the rdma route"));
    }
    if (auto resolved = take_event(RDMA_CM_EVENT_ROUTE_RESOLVED); !resolved) [[unlikely]] {
        return utility::unexpected(resolved.error());
    }

    // The route is known, so the device is too: this is the earliest point the queue
    // pair can be built against it. What is left is the handshake, which is the
    // caller's to start once its receives are posted.
    try {
        BuildQueuePair();
    } catch (const std::exception&) {
        return utility::unexpected(make_error_code(RdmaErrc::kOperationFailed));
    }
    return {};
}

RdmaResult<void> RdmaConnector::Establish() noexcept {
    if (failed()) [[unlikely]] {
        return utility::unexpected(error_);
    }
    if (established_) {
        return {};
    }
    // An active end has nowhere to connect to until BindTarget has resolved the
    // destination and built the queue pair.
    if (!passive_ && queue_pair_ == nullptr) [[unlikely]] {
        return utility::unexpected(make_error_code(RdmaErrc::kNoTarget));
    }
    if (queue_pair_ == nullptr) [[unlikely]] {
        return utility::unexpected(make_error_code(RdmaErrc::kInvalidState));
    }
    // Non-blocking: submit and take at most one event, so the caller keeps control.
    if (non_blocking_) {
        return AdvanceEstablish();
    }
    if (auto submitted = SubmitEstablish(); !submitted) [[unlikely]] {
        return submitted;
    }
    return WaitForEstablish();
}

RdmaResult<void> RdmaConnector::Connect(const net::Address& peer) noexcept {
    if (auto targeted = BindTarget(peer); !targeted) [[unlikely]] {
        return utility::unexpected(targeted.error());
    }
    return Establish();
}
}  // namespace nbio::net

#endif  // defined(NBIO_ENABLE_RDMA) && defined(__linux__)
