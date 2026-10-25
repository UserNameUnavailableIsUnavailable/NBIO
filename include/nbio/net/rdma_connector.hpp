#pragma once
#if defined(NBIO_ENABLE_RDMA) && defined(__linux__)

#include <infiniband/verbs.h>
#include <rdma/rdma_cma.h>

#include <cstddef>
#include <cstdint>
#include <nbio/net/address.hpp>
#include <nbio/net/rdma_resource.hpp>
#include <nbio/net/rdma_result.hpp>
#include <span>
#include <system_error>
#include <utility>
#include <vector>

namespace nbio::net {
class RdmaListener;

/**
 * @brief The sizes a queue pair is built with.
 *
 * @details Both depths bound the number of work requests that may be in flight in
 * their direction, and therefore the number of sequence numbers the connector can
 * hold live at once. They are per connection, not per device.
 */
struct RdmaChannelOptions {
    std::uint32_t send_queue_depth{128};     ///< Work requests the send queue may hold.
    std::uint32_t receive_queue_depth{128};  ///< Work requests the receive queue may hold.
    std::uint32_t max_send_sge{1};           ///< SGEs one send work request may name.
    std::uint32_t max_receive_sge{1};        ///< SGEs one receive work request may name.
    std::uint32_t inline_threshold{0};       ///< Bytes the send queue may carry inline.
};

/**
 * @brief One end of a reliable connection: the queue pair and the memory it may
 *        touch, with no memory of its own.
 *
 * @warning This is infrastructure, not the interface to build against. It is a
 *          primitive: it waits with a blocking @c poll(2) on its own event fd and
 *          knows nothing about the multiplexer, so it must not be driven from the
 *          scheduler thread while the loop is expected to keep running. Use
 *          @ref RdmaSessionService, @ref RdmaListenService or
 *          @ref RdmaConnectService for a connection the event loop drives, and
 *          reach here only when direct control over the queue pair is what is
 *          wanted. See @c docs/rdma.md.
 *
 * @details A connector never allocates a buffer, never carves one, and never
 * decides which buffer is used. It posts work requests naming spans of the
 * caller's regions and hands the same spans back when the device is done with
 * them. Everything above it -- pools, windows, flow control -- is the caller's.
 *
 * The completion model is a proactor's:
 * - @ref PostSend and @ref PostReceive push one work request and answer the
 *   sequence number assigned to it;
 * - @ref WaitSend and @ref WaitReceive block until a sequence number has been
 *   reaped, and answer the highest sequence number that has been;
 * - @ref FetchSend and @ref FetchReceive hand back the span a sequence number
 *   named, with what the device reported about it;
 * - @ref DropSend and @ref DropReceive release the slot so the number can be
 *   reused.
 *
 * @par Sequence numbers
 * Each direction numbers its work requests from one, in posting order. A
 * completion carries its number back in the work request id, so a completion is
 * matched to a request with an index and never with a search.
 *
 * @par Slots and the reorder buffer
 * Slots are indexed by sequence number modulo the direction's queue depth, which
 * is unique among live requests because nothing can be posted while the ring is
 * full. The head advances only over slots that are dropped *and* no longer in
 * flight. Releasing sequence numbers out of order therefore does not free
 * capacity: the ring advances when the oldest live number is released, and a
 * producer that never drops sees @ref RdmaErrc::kQueueFull while the hardware
 * queue is empty.
 *
 * @par Lifetime
 * A buffer named by a posted request must not be read or written by the CPU until
 * its completion has been reaped, and must stay inside the region it came from for
 * at least that long. The connector holds views of the two regions and owns
 * neither.
 *
 * This class is designed to be used in a single thread.
 */
class RdmaConnector {
    friend class RdmaListener;
    friend class RdmaConnectChannel;

   public:
    using Handle = int;

    /**
     * @brief The client's end: creates the id and the channel its own events
     *        arrive on. Nothing is connected yet.
     *
     * @param resource The device, domain and regions every queue pair here is
     *        built from. Must outlive this object.
     * @param options The queue depths and SGE limits to build the queue pair with.
     *
     * @throws std::system_error if the device refuses an id or a channel.
     */
    explicit RdmaConnector(RdmaResource& resource, RdmaChannelOptions options = {});

    RdmaConnector(const RdmaConnector&) = delete;
    RdmaConnector& operator=(const RdmaConnector&) = delete;
    RdmaConnector(RdmaConnector&& other) noexcept;
    RdmaConnector& operator=(RdmaConnector&& other) noexcept;
    ~RdmaConnector() noexcept;

    friend void swap(RdmaConnector& lhs, RdmaConnector& rhs) noexcept;

    /**
     * @brief Pins the local address this connection comes from, before targeting a
     *        destination.
     *
     * @details Left out, the kernel picks along the route. A passive connection was
     * bound by the listener that admitted it, so this is the active end's call.
     *
     * @param local The local address to bind.
     * @return Nothing on success, or why the address could not be bound.
     */
    RdmaResult<void> BindSource(const net::Address& local) noexcept;

    /**
     * @brief Active end: resolves the destination and builds the queue pair,
     *        without connecting.
     *
     * @details This is the targeting stage. It waits for the address and the route
     * to resolve and then creates the two completion queues and the queue pair, so
     * afterwards the receive queue can be posted to -- but the queue pair is not
     * yet in a state where a peer may send, because nothing has been connected.
     *
     * That is exactly the window a receiver needs: post the receives, then call
     * @ref Establish. Between the two, nothing can arrive and nothing is lost.
     *
     * @param peer The address to connect to.
     * @return Nothing once the queue pair exists, or why it does not.
     *
     * @note An active connector has no destination until this has been called, and
     *       @ref Establish answers @ref RdmaErrc::kNoTarget until it has. A passive
     *       connector was targeted by the kernel when the request arrived and
     *       refuses this call.
     */
    RdmaResult<void> BindTarget(const net::Address& peer) noexcept;

    /**
     * @brief Both ends: connects (active) or accepts (passive), then waits for the
     *        connection to be established.
     *
     * @details The queue pair reaches a state where the peer may send as a result
     * of this call and not before it, so every receive that has to be there first
     * must have been posted while the connection was only prepared.
     *
     * In the default, blocking mode this waits for the established event, like
     * @c connect(2) on a blocking socket. In non-blocking mode it submits the
     * request and answers @ref RdmaErrc::kNotReady until the event has been taken;
     * calling it again takes one more step, which is what lets a channel park on the
     * event fd instead of a thread.
     *
     * @return Nothing once the connection is up, or why it is not. Answers nothing
     *         and touches nothing when it is already established. On an active
     *         connector that was never targeted, answers @ref RdmaErrc::kNoTarget --
     *         there is nothing to connect to.
     */
    RdmaResult<void> Establish() noexcept;

    /**
     * @brief Whether @ref Establish may come back before the connection is up.
     *
     * @param enabled Whether to stop waiting.
     */
    void NonBlocking(bool enabled = true) noexcept { non_blocking_ = enabled; }

    /** @brief Whether @ref Establish may return @ref RdmaErrc::kNotReady. */
    bool is_non_blocking() const noexcept { return non_blocking_; }

    /**
     * @brief @ref BindTarget followed by @ref Establish.
     *
     * @param peer The address to connect to.
     * @return Nothing once the connection is established, or why it is not.
     *
     * @note The receive queue is empty when this returns. A caller that has to
     *       post receives before the peer can speak uses the two calls separately.
     */
    RdmaResult<void> Connect(const net::Address& peer) noexcept;

    /**
     * @brief Whether the queue pair exists, and so whether a receive may be posted.
     */
    bool is_prepared() const noexcept { return queue_pair_ != nullptr; }

    /** @brief Whether the connection has been established. */
    bool is_established() const noexcept { return established_; }

    /**
     * @brief Posts the whole of @p buffer as one SEND, and answers its sequence
     *        number.
     *
     * @param buffer A subspan of the resource's send region.
     * @return The sequence number, or @ref RdmaErrc::kInvalidBuffer when the span
     *         is empty, too large for one work request, or outside the send region;
     *         @ref RdmaErrc::kQueueFull when the ring has no free slot;
     *         @ref RdmaErrc::kInvalidState when the connection is not usable; the
     *         connector's error once the stream has failed.
     */
    RdmaResult<RdmaSeq> PostSend(std::span<const char> buffer) noexcept;

    /**
     * @brief Posts the whole of @p buffer as one RECV, and answers its sequence
     *        number.
     *
     * @param buffer A subspan of the resource's receive region.
     * @return The sequence number, or the errors @ref PostSend reports with the
     *         receive region as the range that must contain @p buffer.
     *
     * @note A receive must be posted before the peer sends, or the peer's send
     *       fails with a receiver-not-ready error.
     */
    RdmaResult<RdmaSeq> PostReceive(std::span<char> buffer) noexcept;

    /**
     * @brief Blocks until @p seq has been reaped, and answers the highest sequence
     *        number that has been.
     *
     * @details The answer is a high-water mark, not a boolean: everything at or
     * below it has completed, so a caller that posted several requests learns how
     * far the device has got in one call. Completions are reaped in posting order
     * on a reliable connection, which is what makes the mark meaningful.
     *
     * @param seq The sequence number to wait for.
     * @param timeout_ms @c <0 waits without a deadline, @c 0 reaps and returns,
     *        @c >0 is the longest to wait in milliseconds.
     *
     * @return The highest reaped sequence number. It is below @p seq when the
     *         deadline expired with nothing new reaped -- a timeout is not an error
     *         and says nothing about the connection. An error is returned when
     *         @p seq could never be reaped (@ref RdmaErrc::kInvalidSequence) or the
     *         stream has failed.
     */
    RdmaResult<RdmaSeq> WaitSend(RdmaSeq seq, int timeout_ms = -1) noexcept;

    /**
     * @brief The receive half of @ref WaitSend.
     *
     * @param seq The sequence number to wait for.
     * @param timeout_ms @c <0 waits without a deadline, @c 0 reaps and returns,
     *        @c >0 is the longest to wait in milliseconds.
     * @return The highest reaped sequence number, or why it cannot be reaped.
     */
    RdmaResult<RdmaSeq> WaitReceive(RdmaSeq seq, int timeout_ms = -1) noexcept;

    /**
     * @brief Hands back the span a send sequence number named.
     *
     * @details A read, not a release: it may be asked more than once, and the slot
     * stays live until @ref DropSend. The buffer is the one that was posted, so the
     * caller may reuse it immediately.
     *
     * @param seq The sequence number whose result is wanted.
     * @return The result, or @ref RdmaErrc::kNotReady before the completion has
     *         been reaped, @ref RdmaErrc::kInvalidSequence when the number was
     *         never posted or has been dropped, and the completion's own error when
     *         the work request failed.
     */
    RdmaResult<RdmaSendResult> FetchSend(RdmaSeq seq) noexcept;

    /**
     * @brief Hands back the span a receive sequence number named, and what the peer
     *        wrote into it.
     *
     * @param seq The sequence number whose result is wanted.
     * @return The result, or the errors @ref FetchSend reports.
     */
    RdmaResult<RdmaReceiveResult> FetchReceive(RdmaSeq seq) noexcept;

    /**
     * @brief Releases a send sequence number's slot.
     *
     * @details Legal in any state, including before the completion arrives: a
     * request dropped while in flight has its completion discarded when it comes.
     * Out-of-order drops do not free capacity until the oldest live number is
     * released.
     *
     * @param seq The sequence number to release. Unknown numbers are ignored.
     */
    void DropSend(RdmaSeq seq) noexcept;

    /**
     * @brief Releases a receive sequence number's slot.
     *
     * @param seq The sequence number to release. Unknown numbers are ignored.
     */
    void DropReceive(RdmaSeq seq) noexcept;

    /**
     * @brief Send sequence numbers posted or reaped that have not been dropped.
     *
     * @details This is the ring's occupancy, so it is what bounds posting: a new
     * request is accepted only while it is below the send queue depth.
     */
    std::size_t pending_sends() const noexcept { return send_ring_.occupied(); }

    /** @brief The receive half of @ref pending_sends. */
    std::size_t pending_receives() const noexcept { return receive_ring_.occupied(); }

    /** @brief The fd that becomes readable when a send completion arrives. */
    Handle completion_channel_handle() const noexcept {
        return send_completion_channel_ ? send_completion_channel_->fd : -1;
    }

    /** @brief The fd that becomes readable when a receive completion arrives. */
    Handle receive_completion_channel_handle() const noexcept {
        return receive_completion_channel_ ? receive_completion_channel_->fd : -1;
    }

    /** @brief The fd this connection's management events arrive on. */
    Handle event_channel_handle() const noexcept { return event_channel_ ? event_channel_->fd : -1; }

    /** @brief Whether the peer has ended the connection. */
    bool is_peer_closed() const noexcept { return peer_closed_; }

    /** @brief Whether the stream has failed; every later operation reports it. */
    bool failed() const noexcept { return static_cast<bool>(error_); }

    /** @brief The first failure recorded, empty while the stream is usable. */
    std::error_code error() const noexcept { return error_; }

    /** @brief The resource this connection's domain and regions come from. */
    RdmaResource& resource() noexcept { return *resource_; }

    /** @brief The resource this connection's domain and regions come from. */
    const RdmaResource& resource() const noexcept { return *resource_; }

    /** @brief The queue depths and SGE limits this connection was built with. */
    const RdmaChannelOptions& options() const noexcept { return options_; }

    /**
     * @brief Ends the connection: disconnects, then gives up the id, the channels,
     *        the queue pair and every live sequence number.
     */
    void Close() noexcept;

   private:
    /**
     * @brief One posted work request, from posting to release.
     */
    struct Slot {
        RdmaSeq seq{0};            ///< The sequence number that occupies this slot.
        char* address{nullptr};    ///< The buffer, exactly as posted.
        std::uint32_t length{0};   ///< The length, exactly as posted.
        std::uint32_t bytes{0};    ///< What the completion reported.
        std::error_code status{};  ///< The completion's error, empty on success.
        bool in_flight{false};     ///< The work request has not been reaped.
        bool completed{false};     ///< The completion has been reaped.
        bool dropped{false};       ///< The slot has been released by the caller.
    };

    /**
     * @brief The reorder buffer of one direction.
     */
    class Ring {
       public:
        /**
         * @brief Sizes the ring and drops whatever it held.
         *
         * @param capacity The direction's queue depth.
         */
        void Reset(std::size_t capacity);

        /** @brief Whether another request fits. */
        bool has_room() const noexcept { return occupied() < slots_.size(); }

        /** @brief Live sequence numbers, posted or reaped and not yet dropped. */
        std::size_t occupied() const noexcept { return static_cast<std::size_t>(next_seq_ - head_); }

        /** @brief The number the next post will take. */
        RdmaSeq Next() const noexcept { return next_seq_; }

        /** @brief Consumes the next number after a successful post. */
        void Advance() noexcept { ++next_seq_; }

        /** @brief Whether @p seq has been posted and not advanced past. */
        bool Live(RdmaSeq seq) const noexcept { return seq >= head_ && seq < next_seq_; }

        /** @brief The slot @p seq occupies. Only meaningful while live. */
        Slot& At(RdmaSeq seq) noexcept { return slots_[static_cast<std::size_t>(seq % slots_.size())]; }

        /** @brief The slot @p seq occupies. Only meaningful while live. */
        const Slot& At(RdmaSeq seq) const noexcept { return slots_[static_cast<std::size_t>(seq % slots_.size())]; }

        /**
         * @brief Releases the slots at the head that are dropped and no longer in
         *        flight.
         */
        void AdvanceHead() noexcept;

        RdmaSeq head_{1};         ///< The oldest number not yet released.
        RdmaSeq next_seq_{1};     ///< The number the next post will take.
        RdmaSeq last_reaped_{0};  ///< The highest number reaped so far, 0 for none.
        std::vector<Slot> slots_;  ///< Indexed by number modulo capacity.
    };

    // The listener's end: an id the kernel created for a connection that asked to be
    // admitted, and a channel that id has been migrated onto.
    RdmaConnector(::rdma_cm_id* communication_id, ::rdma_event_channel* event_channel, RdmaResource& resource,
                  RdmaChannelOptions options = {});

    // Waits, on this connection's own channel, for the event that says the handshake
    // finished. Sets established_ when it does.
    RdmaResult<void> WaitForEstablish() noexcept;

    // Posts the connect or accept request, once.
    RdmaResult<void> SubmitEstablish() noexcept;

    // Takes at most one step of the handshake without waiting: submits the request
    // if it has not been submitted, then takes one event if there is one. Answers
    // kNotReady when the connection is not up yet. This is what a channel drives.
    RdmaResult<void> AdvanceEstablish() noexcept;

    // Creates the completion channels, the two completion queues and the queue pair.
    // Posts nothing: what the receive queue holds is the caller's to decide.
    void BuildQueuePair();

    // Gives back everything BuildQueuePair() took, leaving the id and the channel.
    void ReleaseDeviceObjects() noexcept;

    // Asks a completion queue to raise another event.
    RdmaResult<void> Arm(::ibv_cq* queue) noexcept;

    // Reaps whatever is in the send completion queue.
    RdmaResult<std::size_t> ReapSend() noexcept;

    // Reaps whatever is in the receive completion queue.
    RdmaResult<std::size_t> ReapReceive() noexcept;

    // Records one completion in its slot and moves the head on.
    void HandleCompletion(Ring& ring, const ::ibv_wc& completion) noexcept;

    // Empties a completion queue, which is what lets it be destroyed.
    void DrainCompletionQueue(::ibv_cq* queue) noexcept;

    // Consumes the events a completion channel has collected and acknowledges them.
    void DrainEvents(::ibv_comp_channel* channel, ::ibv_cq* queue) noexcept;

    // The shared body of WaitSend and WaitReceive.
    RdmaResult<RdmaSeq> WaitOn(Ring& ring, ::ibv_cq* queue, ::ibv_comp_channel* channel, RdmaSeq seq,
                               int timeout_ms) noexcept;

    // Records the first failure and leaves the stream closed for business.
    void Fail(std::error_code error) noexcept;

    // Tears down everything this stream owns.
    void Reset() noexcept;

    ::rdma_cm_id* communication_id_{nullptr};       // each connection gets an id
    ::rdma_event_channel* event_channel_{nullptr};  // where that id reports its events
    // One completion channel per direction: a channel shared by both would make
    // every wait reap the other direction to avoid leaving an event unconsumed.
    ::ibv_comp_channel* send_completion_channel_{nullptr};
    ::ibv_comp_channel* receive_completion_channel_{nullptr};
    ::ibv_cq* send_completion_queue_{nullptr};
    ::ibv_cq* receive_completion_queue_{nullptr};
    ::ibv_qp* queue_pair_{nullptr};

    // Borrowed for the whole life of the connection: the device, the domain and the
    // two regions every descriptor is made from.
    RdmaResource* resource_{nullptr};

    RdmaChannelOptions options_{};
    Ring send_ring_;
    Ring receive_ring_;

    std::error_code error_{};
    bool peer_closed_{false};
    // Which end of the handshake this is: a passive connection is accepted, an
    // active one connects. It is what Establish() dispatches on.
    bool passive_{false};
    bool established_{false};
    // Whether the connect or accept request has been posted; non-blocking callers
    // submit once and then only take events.
    bool submitted_{false};
    bool non_blocking_{false};
};
}  // namespace nbio::net

#endif  // defined(NBIO_ENABLE_RDMA) && defined(__linux__)
