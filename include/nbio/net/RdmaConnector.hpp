#pragma once
#if defined(NBIO_ENABLE_RDMA) && defined(__linux__)

#include <rdma/rdma_cma.h>

#include <nbio/utility/Bitmap.hpp>
#include <nbio/net/RdmaResult.hpp>
#include <nbio/utility/Expected.hpp>
#include <nbio/net/RdmaResourceManager.hpp>
#include <nbio/net/Address.hpp>
#include <cstddef>
#include <cstdint>
#include <list>
#include <optional>
#include <span>
#include <system_error>

namespace nbio::net {
class RdmaAcceptor;

enum class RdmaSendStatus {
    kDone,
    kPending,
    kPeerClosed,
    kError,
};

// One end of a reliable connection: what a client makes with connect(), and what an
// acceptor hands out once it has admitted a client. It owns the id, the channel that
// id reports its connection-management events on, and the queue pair; the device,
// the protection domain, the memory regions and the chunk pools come from the
// manager it borrows, which must outlive it.
class RdmaConnector {
    friend class RdmaAcceptor;

   public:
    using Handle = int;

    // The client's end: creates the id and the channel its own events arrive on.
    // Nothing is connected yet -- bind() pins the local address if it matters, and
    // connect() finishes the handshake. Only construction may throw, and it does
    // when the device refuses an id or a channel.
    explicit RdmaConnector(RdmaResourceManager& resources);

    // Pins the local address this connection comes from, before connecting. Left
    // out, the kernel picks along the route.
    RdmaResult<void> Bind(const net::Address& local) noexcept;

    // Resolves the peer, builds the queue pair and finishes the handshake. What is
    // left is a connection that can send, receive and close.
    RdmaResult<void> Connect(net::Address peer) noexcept;

    // Both completion queues, and the most work requests either queue may hold.
    static constexpr std::uint32_t kQueueDepth{32};

    // How many sends the user may have in flight, which is also the most of them
    // the device can be holding at once.
    static constexpr std::uint32_t kSendChunks{kQueueDepth};

    static constexpr std::uint32_t kReceiveChunks{kQueueDepth};
    // static_assert(kReceiveChunks > kSendChunks,
    //               "a receiver that posts only as many receives as the sender can fill has nothing left to land in");

    RdmaConnector(const RdmaConnector&) = delete;
    RdmaConnector& operator=(const RdmaConnector&) = delete;
    RdmaConnector(RdmaConnector&& other) noexcept;
    RdmaConnector& operator=(RdmaConnector&& other) noexcept;
    ~RdmaConnector() noexcept;

    friend void swap(RdmaConnector& lhs, RdmaConnector& rhs) noexcept;

    // A chunk to fill, then hand to send(). Several chunks may be acquired at
    // once so multiple sends can be in flight. An empty answer is not a
    // failure: every chunk is either in flight or already filled, and the
    // caller can come back once a completion has retired one.
    RdmaResult<RdmaBufferResult> Acquire() noexcept;

    // Posts the first `length` bytes of `chunk`. The HCA owns them until the
    // completion arrives, so the caller must not touch them until then. Returns
    // without waiting: several sends may be in flight at once.
    RdmaResult<void> Send(std::span<char> chunk, std::size_t length) noexcept;

    // Sends that have been posted and not yet reaped. Every one of them is
    // holding a chunk the caller cannot use again yet.
    std::size_t outstanding_sends() const noexcept { return pending_send_chunks_.size(); }

    // A chunk the peer filled, to read and then hand back. Empty until data has
    // arrived and been polled.
    RdmaResult<RdmaReceiveResult> Receive() noexcept;

    // Returns a received chunk and reposts it for the next message.
    RdmaResult<void> Release(std::span<char> chunk) noexcept;

    // Reaps send completions and answers how many were reaped. 0 polls,
    // negative blocks. A stream that has already failed refuses to poll instead
    // of pretending its completions mean anything.
    RdmaResult<std::size_t> PollSend(int timeout_ms = 0) noexcept;

    // Reaps receive completions. 0 polls, negative blocks.
    RdmaResult<std::size_t> PollReceive(int timeout_ms = 0) noexcept;

    // Reaps both directions. 0 polls, negative blocks.
    RdmaResult<std::size_t> Poll(int timeout_ms = 0) noexcept;

    // Readable whenever a completion is queued: an event, not data.
    Handle completion_channel_handle() const noexcept;

    Handle event_channel_handle() const noexcept;

    // Ends the connection: disconnects, then gives up the id, the channel, the
    // queue pair and the chunks. The destructor calls it.
    void Close() noexcept;

    bool is_peer_closed() const noexcept { return peer_closed_; }

    bool failed() const noexcept { return static_cast<bool>(error_); }

    std::error_code error() const noexcept { return error_; }

   private:
    // The acceptor's end: an id the kernel created for a connection that asked to be
    // admitted, and a channel that id has been migrated onto, so that this
    // connection's events arrive here rather than at the listener.
    RdmaConnector(::rdma_cm_id* communication_id, ::rdma_event_channel* event_channel, RdmaResourceManager& resources);

    // Waits, on this connection's own channel, for the event that says the handshake
    // finished. Nothing else is something to report: the id is the only one on this
    // channel, so an event that is not the established one is that connection's
    // failure.
    RdmaResult<void> WaitForEstablish() noexcept;

    // Creates the completion channel, the two completion queues and the queue pair,
    // takes this connection's chunks from the pools and posts its receives. Throws:
    // the callers are the acceptor's constructor path, where a failure is a
    // rejected connection, and connect(), where it is a failed one.
    void BuildQueuePair();

    // Gives back everything BuildQueuePair() took, leaving the id and the channel
    // with this object.
    void ReleaseDeviceObjects() noexcept;

    // Post all receive chunks.
    void PostAllReceives() noexcept;

    // Asks both completion queues to raise another event. The only thing in
    // this class a provider can refuse, which is why it is the one helper whose
    // failure is reported rather than recorded.
    RdmaResult<void> ArmCompletionQueues() noexcept;
    void DrainCompletionEvents() noexcept;
    void AckCompletionEvents(::ibv_cq* queue) noexcept;
    RdmaResult<std::size_t> PollCompletionQueue(::ibv_cq* queue, int timeout_ms) noexcept;

    void HandleCompletion(const struct ::ibv_wc& completion) noexcept;

    // Empties one completion queue and acknowledges its events, which is what
    // lets the queue be destroyed at all.
    void DrainCompletionQueue(::ibv_cq* queue) noexcept;

    // Records the first failure and leaves the stream closed for business.
    // Later failures are dropped: the first one is the cause, the rest are its
    // consequences.
    void Fail(std::error_code error) noexcept;

    // Hands this connection's chunks back to the shared pools.
    void ReclaimChunks() noexcept;

    // Tears down everything this stream owns.
    void Reset() noexcept;

    ::rdma_cm_id* communication_id_{nullptr};       // each connection gets an id
    ::rdma_event_channel* event_channel_{nullptr};  // where that id reports its events
    ::ibv_comp_channel* completion_channel_{nullptr};
    ::ibv_cq* send_completion_queue_{nullptr};
    ::ibv_cq* receive_completion_queue_{nullptr};
    ::ibv_qp* queue_pair_{nullptr};

    // Borrowed for the whole life of the connection: the device, the protection
    // domain, the two registered regions and the two pools this connection's chunks
    // come from.
    RdmaResourceManager* resources_{nullptr};

    std::uint32_t send_lkey_{0};
    std::uint32_t receive_lkey_{0};

    std::list<utility::BitmapMemory::Chunk> free_send_chunks_;     // available for acquire
    std::list<utility::BitmapMemory::Chunk> busy_send_chunks_;     // acquired, not yet posted
    std::list<utility::BitmapMemory::Chunk> pending_send_chunks_;  // posted, awaiting completion

    std::list<utility::BitmapMemory::Chunk> free_recv_chunks_;                           // available to repost
    std::list<utility::BitmapMemory::Chunk> pending_recv_chunks_;                        // posted, awaiting completion
    std::list<std::pair<utility::BitmapMemory::Chunk, std::size_t>> ready_recv_chunks_;  // FIFO of completed receives
    std::list<utility::BitmapMemory::Chunk> busy_recv_chunks_;  // handed to the user, waiting for release

    unsigned send_unacked_events_{0};
    unsigned receive_unacked_events_{0};
    std::error_code error_{};
    bool peer_closed_{false};
};
}  // namespace nbio::net

#endif  // defined(NBIO_ENABLE_RDMA) && defined(__linux__)

