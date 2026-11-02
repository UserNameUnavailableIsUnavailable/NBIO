#pragma once
#if defined(NBIO_ENABLE_RDMA) && defined(__linux__)

#include <cstdint>
#include <deque>
#include <memory>
#include <nbio/async/coroutine.hpp>
#include <nbio/async/task.hpp>
#include <nbio/net/rdma_header.hpp>
#include <nbio/net/rdma_result.hpp>
#include <nbio/net/rdma_session_service.hpp>
#include <nbio/notification/condition_variable.hpp>
#include <nbio/utility/expected.hpp>
#include <span>
#include <vector>

namespace nbio::net {
/**
 * @brief A stream over a connection, with a window and acknowledgements.
 *
 * @details This is the one place a buffer policy survives, and it is deliberately
 * above the transport: the service carves the session's regions into packet-sized
 * slots itself, and the connector and the channels know nothing about it.
 *
 * - Every packet carries the highest sequence number this end has finished with,
 *   which is what lets the peer send again.
 * - A payload's slot is not reposted until the caller releases the payload, so an
 *   acknowledgement means "the buffer is back", not "the bytes went past".
 * - The window is the number of receives this end has posted, announced in the
 *   handshake, so neither end has to guess what the other can take.
 */
class RdmaDeliverService final : public std::enable_shared_from_this<RdmaDeliverService> {
   public:
    /**
     * @brief What one end can take: the bytes in one packet, and how many packets
     *        it has posted.
     */
    struct Layout {
        std::uint64_t chunk_size{0};   ///< Bytes in one posted receive.
        std::uint64_t chunk_count{0};  ///< Receives posted, which is the window.

        /** @brief What one packet's payload can be. */
        std::uint64_t payload_size() const noexcept {
            return chunk_size > sizeof(nbio::net::RdmaHeader) ? chunk_size - sizeof(nbio::net::RdmaHeader) : 0;
        }
    };

    /**
     * @brief Carves the session's regions into slots and posts every receive.
     *
     * @param session The established connection to carry the stream.
     * @param layout What this end can take.
     *
     * @throws std::invalid_argument when the layout cannot be honoured.
     */
    RdmaDeliverService(std::shared_ptr<RdmaSessionService> session, Layout layout);
    ~RdmaDeliverService() noexcept;

    RdmaDeliverService(const RdmaDeliverService&) = delete;
    RdmaDeliverService& operator=(const RdmaDeliverService&) = delete;
    RdmaDeliverService(RdmaDeliverService&&) = delete;
    RdmaDeliverService& operator=(RdmaDeliverService&&) = delete;

    /** @brief Says what this end can take and waits to hear the same from the peer. */
    nbio::async::Task<RdmaResult<void>> handshake();

    /** @brief Starts the reader. Until this is called nothing is acknowledged. */
    void Start();

    /** @brief Ends the link from this side and cancels the reader. */
    void Stop() noexcept;

    /** @brief Sends one payload, cut into as many packets as it needs. */
    nbio::async::Task<RdmaResult<void>> Send(std::span<const char> payload);

    /** @brief The next payload the peer sent, or that the link is over. */
    nbio::async::Task<RdmaResult<RdmaPayloadResult>> Receive();

    /** @brief Gives a payload back, which reposts its slot. */
    nbio::async::Task<RdmaResult<void>> release(std::span<char> payload);

    std::uint64_t acknowledged() const noexcept { return acknowledged_; }
    std::uint64_t sent() const noexcept { return sent_; }
    std::uint64_t taken() const noexcept { return taken_; }
    const Layout& layout() const noexcept { return mine_; }

   private:
    struct Incoming {
        std::span<char> packet{};
        std::span<char> chunk{};
        std::uint64_t sequence{0};
        nbio::net::RdmaPacketType type{};
        std::uint64_t acknowledge{0};
    };

    struct Held {
        std::span<char> payload{};
        std::span<char> chunk{};
        std::uint64_t sequence{0};
    };

    nbio::async::Task<RdmaResult<RdmaReceiveResult>> PullPacket();
    nbio::async::Task<RdmaResult<Incoming>> ReadPacket();
    nbio::async::Task<RdmaResult<void>> Absorb(Incoming packet);
    nbio::async::Task<RdmaResult<void>> SendPacket(std::span<const char> payload,
                                                   nbio::net::RdmaPacketType type);
    nbio::async::Task<RdmaResult<void>> WaitForRoom(std::uint64_t packets);

    bool RoomFor(std::uint64_t packets) const noexcept;
    RdmaResult<void> Repost(std::span<char> chunk) noexcept;
    static nbio::async::Task<void> Read(std::shared_ptr<RdmaDeliverService> self);

    std::shared_ptr<RdmaSessionService> session_;
    Layout mine_;
    Layout peer_{};
    bool handshaken_{false};
    bool ended_{false};

    std::uint64_t sent_{0};
    std::uint64_t acknowledged_{0};
    std::uint64_t taken_{0};
    std::uint64_t released_{0};

    std::deque<Held> ready_;
    std::uint64_t acknowledged_to_peer_{0};
    nbio::notification::ConditionVariable room_;
    nbio::notification::ConditionVariable ready_available_;

    std::vector<std::span<char>> receive_slots_;
    RdmaSeq next_receive_seq_{0};

    std::vector<std::span<char>> send_slots_;
    std::vector<RdmaSeq> send_slot_seq_;
    std::size_t send_cursor_{0};

    nbio::async::CoroutineJoinHandle reader_{};
};
}  // namespace nbio::net

#endif  // defined(NBIO_ENABLE_RDMA) && defined(__linux__)
