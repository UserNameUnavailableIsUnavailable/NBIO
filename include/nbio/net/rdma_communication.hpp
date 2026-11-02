#pragma once
#if defined(NBIO_ENABLE_RDMA) && defined(__linux__)

#include <infiniband/verbs.h>

#include <cstddef>
#include <cstdint>
#include <nbio/net/rdma_resource.hpp>
#include <nbio/net/rdma_result.hpp>
#include <span>
#include <system_error>
#include <utility>
#include <vector>

struct rdma_cm_id;
struct rdma_event_channel;

namespace nbio::net {
/**
 * @brief The sizes a queue pair is built with.
 *
 * @details Both depths bound the number of work requests that may be in flight in
 * their direction, and therefore the number of sequence numbers live at once.
 */
struct RdmaChannelOptions {
    std::uint32_t send_queue_depth{128};
    std::uint32_t receive_queue_depth{128};
    std::uint32_t max_send_sge{1};
    std::uint32_t max_receive_sge{1};
    std::uint32_t inline_threshold{0};
};

namespace detail {
// One posted work request, from posting to release.
struct RdmaSlot {
    RdmaSeq seq{0};
    char* address{nullptr};
    std::uint32_t length{0};
    std::uint32_t bytes{0};
    std::error_code status{};
    bool in_flight{false};
    bool completed{false};
    bool dropped{false};
};

// The reorder buffer of one direction. Slots are indexed by sequence number modulo
// capacity; the head advances only over slots that are dropped and no longer in
// flight, so a late completion can never land on a re-posted slot.
class RdmaRing {
   public:
    void Reset(std::size_t capacity);
    bool has_room() const noexcept { return occupied() < slots_.size(); }
    std::size_t occupied() const noexcept { return static_cast<std::size_t>(next_seq_ - head_); }
    RdmaSeq Next() const noexcept { return next_seq_; }
    void Advance() noexcept { ++next_seq_; }
    bool Live(RdmaSeq seq) const noexcept { return seq >= head_ && seq < next_seq_; }
    RdmaSlot& At(RdmaSeq seq) noexcept { return slots_[static_cast<std::size_t>(seq % slots_.size())]; }
    const RdmaSlot& At(RdmaSeq seq) const noexcept { return slots_[static_cast<std::size_t>(seq % slots_.size())]; }
    void AdvanceHead() noexcept;

    RdmaSeq head_{1};
    RdmaSeq next_seq_{1};
    RdmaSeq last_reaped_{0};
    std::vector<RdmaSlot> slots_;
};
}  // namespace detail

class RdmaConnector;
class RdmaListener;
class RdmaEstablishChannel;
class RdmaSendChannel;
class RdmaReceiveChannel;

/**
 * @brief The queue pair and the memory it may touch, with no memory of its own.
 *
 * @details This is the data plane. It never allocates, carves or frees a buffer;
 * it posts work requests naming spans of the caller's regions and hands the same
 * spans back when the device is done with them. Everything above it is the
 * caller's.
 *
 * @warning This is a primitive, not the interface to build against. Its
 * operations block and it knows nothing about the multiplexer. Use
 * @ref RdmaSessionService, @ref RdmaListenService or @ref RdmaConnectService, and
 * reach here only for direct control. See @c docs/rdma.md.
 */
class RdmaCommunication {
    friend class RdmaConnector;
    friend class RdmaListener;
    friend class RdmaEstablishChannel;
    friend class RdmaSendChannel;
    friend class RdmaReceiveChannel;

   public:
    using Handle = int;

    RdmaCommunication(const RdmaCommunication&) = delete;
    RdmaCommunication& operator=(const RdmaCommunication&) = delete;
    RdmaCommunication(RdmaCommunication&& other) noexcept;
    RdmaCommunication& operator=(RdmaCommunication&& other) noexcept;
    ~RdmaCommunication() noexcept;

    friend void swap(RdmaCommunication& lhs, RdmaCommunication& rhs) noexcept;

    /**
     * @brief Finishes the connection: rdma_connect on the active end, rdma_accept
     *        on the passive one, then the established event.
     *
     * @details Blocks until the connection is up. What has to be posted first is
     * the caller's to post first.
     */
    RdmaResult<void> Establish() noexcept;

    /** @brief Posts the whole of @p buffer as one SEND, and answers its sequence number. */
    RdmaResult<RdmaSeq> SubmitSend(std::span<const char> buffer) noexcept;

    /** @brief Posts the whole of @p buffer as one RECV, and answers its sequence number. */
    RdmaResult<RdmaSeq> SubmitReceive(std::span<char> buffer) noexcept;

    /**
     * @brief Waits until @p seq has been reaped and answers the highest reaped
     *        sequence number. @p timeout_ms < 0 waits without a deadline, 0 reaps
     *        once, > 0 is a deadline in milliseconds. A timeout is not an error.
     */
    RdmaResult<RdmaSeq> WaitSend(RdmaSeq seq, int timeout_ms = -1) noexcept;
    RdmaResult<RdmaSeq> WaitReceive(RdmaSeq seq, int timeout_ms = -1) noexcept;

    /** @brief The span a send sequence number named; may be asked more than once. */
    RdmaResult<RdmaSendResult> PeekSend(RdmaSeq seq) noexcept;

    /** @brief The span and byte count a receive sequence number named. */
    RdmaResult<RdmaReceiveResult> PeekReceive(RdmaSeq seq) noexcept;

    /** @brief Releases a send sequence number's slot. */
    void CompleteSend(RdmaSeq seq) noexcept;

    /** @brief Releases a receive sequence number's slot. */
    void CompleteReceive(RdmaSeq seq) noexcept;

    /** @brief Ends the connection and releases everything it owns. */
    void Reset() noexcept;

    /** @brief The fd that becomes readable when a send completion arrives. */
    Handle completion_channel_handle() const noexcept;
    /** @brief The fd that becomes readable when a receive completion arrives. */
    Handle receive_completion_channel_handle() const noexcept;
    /** @brief The fd the connection-management events arrive on. */
    Handle event_channel_handle() const noexcept;

    /** @brief Whether the connection has been established. */
    bool is_established() const noexcept { return established_; }
    /** @brief Whether the stream has failed. */
    bool failed() const noexcept { return static_cast<bool>(error_); }
    /** @brief The first failure recorded. */
    std::error_code error() const noexcept { return error_; }
    /** @brief The resource the queue pair and regions were built from. */
    RdmaResource& resource() noexcept { return *resource_; }
    /** @brief The queue depths and SGE limits this connection was built with. */
    const RdmaChannelOptions& options() const noexcept { return options_; }

   private:
    RdmaCommunication(::rdma_cm_id* communication_id, ::rdma_event_channel* event_channel, RdmaResource& resource,
                      RdmaChannelOptions options, bool passive);

    void BuildQueuePair();
    void ReleaseDeviceObjects() noexcept;
    RdmaResult<void> SubmitEstablish() noexcept;
    RdmaResult<void> WaitForEstablish() noexcept;
    RdmaResult<void> AdvanceEstablish(int timeout_ms) noexcept;
    RdmaResult<void> Arm(::ibv_cq* queue) noexcept;
    RdmaResult<std::size_t> ReapSend() noexcept;
    RdmaResult<std::size_t> ReapReceive() noexcept;
    void HandleCompletion(detail::RdmaRing& ring, const ::ibv_wc& completion) noexcept;
    void DrainCompletionQueue(::ibv_cq* queue) noexcept;
    void DrainEvents(::ibv_comp_channel* channel, ::ibv_cq* queue) noexcept;
    RdmaResult<RdmaSeq> WaitOn(detail::RdmaRing& ring, ::ibv_cq* queue, ::ibv_comp_channel* channel, RdmaSeq seq,
                               int timeout_ms) noexcept;
    void Fail(std::error_code error) noexcept;

    ::rdma_cm_id* communication_id_{nullptr};
    ::rdma_event_channel* event_channel_{nullptr};
    ::ibv_comp_channel* send_completion_channel_{nullptr};
    ::ibv_comp_channel* receive_completion_channel_{nullptr};
    ::ibv_cq* send_completion_queue_{nullptr};
    ::ibv_cq* receive_completion_queue_{nullptr};
    ::ibv_qp* queue_pair_{nullptr};
    RdmaResource* resource_{nullptr};
    RdmaChannelOptions options_{};
    detail::RdmaRing send_ring_;
    detail::RdmaRing receive_ring_;

    bool passive_{false};
    bool established_{false};
    bool submitted_{false};
    std::error_code error_{};
};
}  // namespace nbio::net

#endif  // defined(NBIO_ENABLE_RDMA) && defined(__linux__)
