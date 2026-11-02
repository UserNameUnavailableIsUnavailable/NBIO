#if defined(NBIO_ENABLE_RDMA) && defined(__linux__)

#include <fcntl.h>
#include <infiniband/verbs.h>
#include <poll.h>
#include <rdma/rdma_cma.h>

#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <limits>
#include <nbio/net/rdma_communication.hpp>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>

namespace nbio::net {
namespace {
std::error_code Failing(const char* what) { return {errno, std::system_category()}; }

std::string DescribeCmEvent(const ::rdma_cm_event& event) {
    std::string description = ::rdma_event_str(event.event);
    if (event.status != 0) {
        description += " (status=" + std::to_string(event.status) + ")";
    }
    return description;
}

// Waits for one event, polling first so a blocking fd never traps a caller that
// asked not to wait.
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

bool IsEstablishFailure(::rdma_cm_event_type type) noexcept {
    return type == RDMA_CM_EVENT_REJECTED || type == RDMA_CM_EVENT_UNREACHABLE ||
           type == RDMA_CM_EVENT_CONNECT_ERROR || type == RDMA_CM_EVENT_ADDR_ERROR ||
           type == RDMA_CM_EVENT_ROUTE_ERROR || type == RDMA_CM_EVENT_DEVICE_REMOVAL;
}
}  // namespace

void detail::RdmaRing::Reset(std::size_t capacity) {
    slots_.assign(capacity == 0 ? 1 : capacity, RdmaSlot{});
    head_ = 1;
    next_seq_ = 1;
    last_reaped_ = 0;
}

void detail::RdmaRing::AdvanceHead() noexcept {
    while (head_ < next_seq_) {
        const RdmaSlot& slot = At(head_);
        if (!slot.dropped || slot.in_flight || slot.seq != head_) {
            break;
        }
        At(head_) = RdmaSlot{};
        ++head_;
    }
}

RdmaCommunication::RdmaCommunication(::rdma_cm_id* communication_id, ::rdma_event_channel* event_channel,
                                     RdmaResource& resource, RdmaChannelOptions options, bool passive)
    : communication_id_(communication_id),
      event_channel_(event_channel),
      resource_(&resource),
      options_(options),
      passive_(passive) {
    if (!communication_id_ || !event_channel_) {
        throw std::invalid_argument("an rdma communication needs an id and a channel of its own");
    }
    send_ring_.Reset(options_.send_queue_depth);
    receive_ring_.Reset(options_.receive_queue_depth);
    BuildQueuePair();
}

RdmaCommunication::RdmaCommunication(RdmaCommunication&& other) noexcept { swap(*this, other); }

RdmaCommunication& RdmaCommunication::operator=(RdmaCommunication&& other) noexcept {
    if (this != &other) {
        Reset();
        swap(*this, other);
    }
    return *this;
}

void swap(RdmaCommunication& lhs, RdmaCommunication& rhs) noexcept {
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
    swap(lhs.passive_, rhs.passive_);
    swap(lhs.established_, rhs.established_);
    swap(lhs.submitted_, rhs.submitted_);
    swap(lhs.error_, rhs.error_);
}

RdmaCommunication::~RdmaCommunication() noexcept { Reset(); }

void RdmaCommunication::BuildQueuePair() {
    try {
        send_completion_channel_ = ::ibv_create_comp_channel(communication_id_->verbs);
        if (!send_completion_channel_) throw std::runtime_error("Failed to create the rdma send completion channel");
        receive_completion_channel_ = ::ibv_create_comp_channel(communication_id_->verbs);
        if (!receive_completion_channel_) throw std::runtime_error("Failed to create the rdma receive completion channel");
        for (auto* channel : {send_completion_channel_, receive_completion_channel_}) {
            const int flags = ::fcntl(channel->fd, F_GETFL, 0);
            if (flags < 0 || ::fcntl(channel->fd, F_SETFL, flags | O_NONBLOCK) < 0) {
                throw std::runtime_error("Failed to make an rdma completion channel non-blocking");
            }
        }

        send_completion_queue_ = ::ibv_create_cq(communication_id_->verbs, static_cast<int>(options_.send_queue_depth),
                                                 nullptr, send_completion_channel_, 0);
        if (!send_completion_queue_) throw std::runtime_error("Failed to create the rdma send completion queue");
        receive_completion_queue_ = ::ibv_create_cq(communication_id_->verbs,
                                                    static_cast<int>(options_.receive_queue_depth), nullptr,
                                                    receive_completion_channel_, 0);
        if (!receive_completion_queue_) throw std::runtime_error("Failed to create the rdma receive completion queue");

        ibv_qp_init_attr attributes{
            .qp_context = nullptr,
            .send_cq = send_completion_queue_,
            .recv_cq = receive_completion_queue_,
            .srq = nullptr,
            .cap = {.max_send_wr = options_.send_queue_depth,
                    .max_recv_wr = options_.receive_queue_depth,
                    .max_send_sge = options_.max_send_sge,
                    .max_recv_sge = options_.max_receive_sge,
                    .max_inline_data = options_.inline_threshold},
            .qp_type = IBV_QPT_RC,
            .sq_sig_all = 0,
        };
        if (::rdma_create_qp(communication_id_, resource_->protection_domain(), &attributes) != 0) {
            throw std::runtime_error("Failed to create the rdma queue pair");
        }
        queue_pair_ = communication_id_->qp;

        if (auto armed = Arm(send_completion_queue_); !armed) [[unlikely]] {
            throw std::system_error(armed.error(), "Failed to arm the rdma send completion queue");
        }
        if (auto armed = Arm(receive_completion_queue_); !armed) [[unlikely]] {
            throw std::system_error(armed.error(), "Failed to arm the rdma receive completion queue");
        }
    } catch (...) {
        ReleaseDeviceObjects();
        throw;
    }
}

void RdmaCommunication::ReleaseDeviceObjects() noexcept {
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

void RdmaCommunication::Reset() noexcept {
    if (communication_id_ && !established_ && passive_ && queue_pair_) {
        ::rdma_reject(communication_id_, nullptr, 0);
    } else if (communication_id_ && established_ && queue_pair_) {
        ::rdma_disconnect(communication_id_);
    }
    if (queue_pair_) {
        ::rdma_destroy_qp(communication_id_);
        queue_pair_ = nullptr;
    }
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
    send_ring_.Reset(options_.send_queue_depth);
    receive_ring_.Reset(options_.receive_queue_depth);
}

void RdmaCommunication::DrainCompletionQueue(::ibv_cq* queue) noexcept {
    if (!queue) return;
    ibv_wc completions[16];
    while (::ibv_poll_cq(queue, 16, completions) > 0) {
    }
    ::ibv_comp_channel* channel = queue == send_completion_queue_ ? send_completion_channel_ : receive_completion_channel_;
    if (channel) {
        ::ibv_cq* notified{nullptr};
        void* context{nullptr};
        unsigned count = 0;
        while (::ibv_get_cq_event(channel, &notified, &context) == 0) ++count;
        if (count != 0) ::ibv_ack_cq_events(queue, count);
    }
}

RdmaResult<void> RdmaCommunication::Arm(::ibv_cq* queue) noexcept {
    if (queue && ::ibv_req_notify_cq(queue, 0) != 0) [[unlikely]] {
        return utility::unexpected(Failing("Failed to arm an rdma completion queue"));
    }
    return {};
}

void RdmaCommunication::DrainEvents(::ibv_comp_channel* channel, ::ibv_cq* queue) noexcept {
    if (!channel || !queue) return;
    ::ibv_cq* notified{nullptr};
    void* context{nullptr};
    unsigned count = 0;
    while (::ibv_get_cq_event(channel, &notified, &context) == 0) ++count;
    if (count != 0) ::ibv_ack_cq_events(queue, count);
}

void RdmaCommunication::HandleCompletion(detail::RdmaRing& ring, const ::ibv_wc& completion) noexcept {
    const RdmaSeq seq = completion.wr_id;
    if (!ring.Live(seq)) return;
    detail::RdmaSlot& slot = ring.At(seq);
    if (slot.seq != seq) return;
    slot.in_flight = false;
    slot.bytes = completion.byte_len;
    if (completion.status != IBV_WC_SUCCESS) [[unlikely]] {
        if (completion.status == IBV_WC_WR_FLUSH_ERR) {
            slot.status = make_error_code(RdmaErrc::kPeerClosed);
        } else {
            Fail(make_error_code(RdmaErrc::kCompletionFailed));
            slot.status = error_;
        }
    }
    if (!slot.dropped) slot.completed = true;
    if (seq > ring.last_reaped_) ring.last_reaped_ = seq;
    ring.AdvanceHead();
}

RdmaResult<std::size_t> RdmaCommunication::ReapSend() noexcept {
    if (!send_completion_queue_) return std::size_t{0};
    std::size_t reaped = 0;
    ibv_wc completions[16];
    while (true) {
        const int count = ::ibv_poll_cq(send_completion_queue_, 16, completions);
        if (count <= 0) break;
        for (int i = 0; i < count; ++i) HandleCompletion(send_ring_, completions[i]);
        reaped += static_cast<std::size_t>(count);
    }
    return reaped;
}

RdmaResult<std::size_t> RdmaCommunication::ReapReceive() noexcept {
    if (!receive_completion_queue_) return std::size_t{0};
    std::size_t reaped = 0;
    ibv_wc completions[16];
    while (true) {
        const int count = ::ibv_poll_cq(receive_completion_queue_, 16, completions);
        if (count <= 0) break;
        for (int i = 0; i < count; ++i) HandleCompletion(receive_ring_, completions[i]);
        reaped += static_cast<std::size_t>(count);
    }
    return reaped;
}

RdmaResult<RdmaSeq> RdmaCommunication::SubmitSend(std::span<const char> buffer) noexcept {
    if (failed()) return utility::unexpected(error_);
    if (queue_pair_ == nullptr) return utility::unexpected(make_error_code(RdmaErrc::kInvalidState));
    if (buffer.empty() || buffer.size() > std::numeric_limits<std::uint32_t>::max() ||
        !Contains(resource_->send_memory_region(), buffer)) {
        return utility::unexpected(make_error_code(RdmaErrc::kInvalidBuffer));
    }
    if (!send_ring_.has_room()) return utility::unexpected(make_error_code(RdmaErrc::kQueueFull));

    const RdmaSeq seq = send_ring_.Next();
    detail::RdmaSlot& slot = send_ring_.At(seq);
    slot = detail::RdmaSlot{.seq = seq,
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
        slot = detail::RdmaSlot{};
        return utility::unexpected(std::error_code{code, std::generic_category()});
    }
    send_ring_.Advance();
    return seq;
}

RdmaResult<RdmaSeq> RdmaCommunication::SubmitReceive(std::span<char> buffer) noexcept {
    if (failed()) return utility::unexpected(error_);
    if (queue_pair_ == nullptr) return utility::unexpected(make_error_code(RdmaErrc::kInvalidState));
    if (buffer.empty() || buffer.size() > std::numeric_limits<std::uint32_t>::max() ||
        !Contains(resource_->receive_memory_region(), buffer)) {
        return utility::unexpected(make_error_code(RdmaErrc::kInvalidBuffer));
    }
    if (!receive_ring_.has_room()) return utility::unexpected(make_error_code(RdmaErrc::kQueueFull));

    const RdmaSeq seq = receive_ring_.Next();
    detail::RdmaSlot& slot = receive_ring_.At(seq);
    slot = detail::RdmaSlot{.seq = seq,
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
        slot = detail::RdmaSlot{};
        return utility::unexpected(std::error_code{code, std::generic_category()});
    }
    receive_ring_.Advance();
    return seq;
}

RdmaResult<RdmaSeq> RdmaCommunication::WaitOn(detail::RdmaRing& ring, ::ibv_cq* queue, ::ibv_comp_channel* channel,
                                              RdmaSeq seq, int timeout_ms) noexcept {
    if (seq == 0 || seq >= ring.Next()) return utility::unexpected(make_error_code(RdmaErrc::kInvalidSequence));
    if (ring.last_reaped_ >= seq) return ring.last_reaped_;
    if (failed()) return utility::unexpected(error_);
    if (queue == nullptr || channel == nullptr) return utility::unexpected(make_error_code(RdmaErrc::kInvalidState));

    if (auto armed = Arm(queue); !armed) return utility::unexpected(armed.error());
    while (true) {
        if (queue == send_completion_queue_) {
            (void)ReapSend();
        } else {
            (void)ReapReceive();
        }
        if (ring.last_reaped_ >= seq) return ring.last_reaped_;
        if (failed()) return utility::unexpected(error_);
        if (timeout_ms == 0) return ring.last_reaped_;

        pollfd waiter{.fd = channel->fd, .events = POLLIN, .revents = 0};
        if (::poll(&waiter, 1, timeout_ms) <= 0) return ring.last_reaped_;
        DrainEvents(channel, queue);
        if (auto armed = Arm(queue); !armed) return utility::unexpected(armed.error());
    }
}

RdmaResult<RdmaSeq> RdmaCommunication::WaitSend(RdmaSeq seq, int timeout_ms) noexcept {
    return WaitOn(send_ring_, send_completion_queue_, send_completion_channel_, seq, timeout_ms);
}

RdmaResult<RdmaSeq> RdmaCommunication::WaitReceive(RdmaSeq seq, int timeout_ms) noexcept {
    return WaitOn(receive_ring_, receive_completion_queue_, receive_completion_channel_, seq, timeout_ms);
}

RdmaResult<RdmaSendResult> RdmaCommunication::PeekSend(RdmaSeq seq) noexcept {
    if (seq == 0 || seq >= send_ring_.Next()) return utility::unexpected(make_error_code(RdmaErrc::kInvalidSequence));
    const detail::RdmaSlot& slot = send_ring_.At(seq);
    if (slot.seq != seq || slot.dropped) return utility::unexpected(make_error_code(RdmaErrc::kInvalidSequence));
    if (!slot.completed) return utility::unexpected(make_error_code(RdmaErrc::kNotReady));
    if (slot.status) return utility::unexpected(slot.status);
    return RdmaSendResult{.seq = seq, .buffer = std::span<char>(slot.address, slot.length)};
}

RdmaResult<RdmaReceiveResult> RdmaCommunication::PeekReceive(RdmaSeq seq) noexcept {
    if (seq == 0 || seq >= receive_ring_.Next()) return utility::unexpected(make_error_code(RdmaErrc::kInvalidSequence));
    const detail::RdmaSlot& slot = receive_ring_.At(seq);
    if (slot.seq != seq || slot.dropped) return utility::unexpected(make_error_code(RdmaErrc::kInvalidSequence));
    if (!slot.completed) return utility::unexpected(make_error_code(RdmaErrc::kNotReady));
    if (slot.status) return utility::unexpected(slot.status);
    return RdmaReceiveResult{.seq = seq, .buffer = std::span<char>(slot.address, slot.length), .bytes = slot.bytes};
}

void RdmaCommunication::CompleteSend(RdmaSeq seq) noexcept {
    if (seq == 0 || seq >= send_ring_.Next()) return;
    detail::RdmaSlot& slot = send_ring_.At(seq);
    if (slot.seq != seq) return;
    slot.dropped = true;
    send_ring_.AdvanceHead();
}

void RdmaCommunication::CompleteReceive(RdmaSeq seq) noexcept {
    if (seq == 0 || seq >= receive_ring_.Next()) return;
    detail::RdmaSlot& slot = receive_ring_.At(seq);
    if (slot.seq != seq) return;
    slot.dropped = true;
    receive_ring_.AdvanceHead();
}

RdmaResult<void> RdmaCommunication::SubmitEstablish() noexcept {
    if (submitted_) return {};
    ::rdma_conn_param param{};
    param.responder_resources = 1;
    param.initiator_depth = 1;
    param.retry_count = 7;
    param.rnr_retry_count = 7;
    const int code = passive_ ? ::rdma_accept(communication_id_, &param) : ::rdma_connect(communication_id_, &param);
    if (code != 0) [[unlikely]] {
        return utility::unexpected(Failing("Failed to establish the rdma connection"));
    }
    submitted_ = true;
    return {};
}

RdmaResult<void> RdmaCommunication::WaitForEstablish() noexcept {
    while (true) {
        auto event = WaitCmEvent(event_channel_, -1);
        if (!event) return utility::unexpected(event.error());
        const auto type = (*event)->event;
        const auto description = DescribeCmEvent(*(*event));
        ::rdma_ack_cm_event(*event);
        if (type == RDMA_CM_EVENT_ESTABLISHED) {
            established_ = true;
            return {};
        }
        if (IsEstablishFailure(type)) [[unlikely]] {
            return utility::unexpected(make_error_code(RdmaErrc::kOperationFailed));
        }
    }
}

RdmaResult<void> RdmaCommunication::AdvanceEstablish(int timeout_ms) noexcept {
    if (established_) return {};
    if (queue_pair_ == nullptr) return utility::unexpected(make_error_code(RdmaErrc::kInvalidState));
    if (auto submitted = SubmitEstablish(); !submitted) return submitted;

    auto event = WaitCmEvent(event_channel_, timeout_ms);
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
    if (IsEstablishFailure(type)) [[unlikely]] {
        return utility::unexpected(make_error_code(RdmaErrc::kOperationFailed));
    }
    return utility::unexpected(make_error_code(RdmaErrc::kNotReady));
}

RdmaResult<void> RdmaCommunication::Establish() noexcept {
    if (failed()) return utility::unexpected(error_);
    if (established_) return {};
    if (queue_pair_ == nullptr) return utility::unexpected(make_error_code(RdmaErrc::kInvalidState));
    if (auto submitted = SubmitEstablish(); !submitted) return submitted;
    return WaitForEstablish();
}

void RdmaCommunication::Fail(std::error_code error) noexcept {
    if (!error_) error_ = error;
}

RdmaCommunication::Handle RdmaCommunication::completion_channel_handle() const noexcept {
    return send_completion_channel_ ? send_completion_channel_->fd : -1;
}

RdmaCommunication::Handle RdmaCommunication::receive_completion_channel_handle() const noexcept {
    return receive_completion_channel_ ? receive_completion_channel_->fd : -1;
}

RdmaCommunication::Handle RdmaCommunication::event_channel_handle() const noexcept {
    return event_channel_ ? event_channel_->fd : -1;
}
}  // namespace nbio::net

#endif  // defined(NBIO_ENABLE_RDMA) && defined(__linux__)
