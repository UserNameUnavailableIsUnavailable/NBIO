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
#include <string>
#include <vector>

namespace nbio::net {
/**
 * @brief A stream over a connection, with a window and acknowledgements.
 *
 * @details This is the one place a buffer policy survives, and it is deliberately
 * above the transport: the service carves the session's regions into packetsized
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
     * @param layout What this end can take. Must fit the session's receive region,
     *        fit within its receive queue, and leave room for one packet in its
     *        send region.
     *
     * @throws std::invalid_argument when the layout cannot be honoured, which is a
     *         programming error rather than a link failure.
     */
    RdmaDeliverService(std::shared_ptr<RdmaSessionService> session, Layout layout);
    ~RdmaDeliverService() noexcept;

    RdmaDeliverService(const RdmaDeliverService&) = delete;
    RdmaDeliverService& operator=(const RdmaDeliverService&) = delete;
    RdmaDeliverService(RdmaDeliverService&&) = delete;
    RdmaDeliverService& operator=(RdmaDeliverService&&) = delete;

    /**
     * @brief Says what this end can take and waits to hear the same from the peer.
     *
     * @return Nothing once both ends agree, or why the link could not be opened.
     */
    nbio::async::Task<nbio::utility::expected<void, std::string>> handshake();

    /** @brief Starts the reader. Until this is called nothing is acknowledged. */
    void Start();

    /** @brief Ends the link from this side and cancels the reader. */
    void Stop() noexcept;

    /**
     * @brief Sends one payload, cut into as many packets as it needs.
     *
     * @param payload The bytes to send.
     * @return Nothing once every packet has been handed to the device, or why not.
     */
    nbio::async::Task<nbio::utility::expected<void, std::string>> Send(std::span<const char> payload);

    /** @brief The next payload the peer sent, or that the link is over. */
    nbio::async::Task<nbio::utility::expected<RdmaPayloadResult, std::string>> Receive();

    /**
     * @brief Gives a payload back, which reposts its slot and is what the peer waits
     *        for.
     *
     * @param payload The payload previously answered by @ref Receive, in arrival
     *        order.
     * @return Nothing once the slot is posted again, or why it is not.
     */
    nbio::async::Task<nbio::utility::expected<void, std::string>> release(std::span<char> payload);

    /** @brief What the peer has acknowledged. */
    std::uint64_t acknowledged() const noexcept { return acknowledged_; }

    /** @brief What this end has sent. */
    std::uint64_t sent() const noexcept { return sent_; }

    /** @brief What this end has taken. */
    std::uint64_t taken() const noexcept { return taken_; }

    /** @brief The layout this end announced. */
    const Layout& layout() const noexcept { return mine_; }

   private:
    /**
     * @brief One packet, as it arrived: the filled part, and the slot it came in.
     */
    struct Incoming {
        std::span<char> packet{};  ///< Header and payload, as many bytes as arrived.
        std::span<char> chunk{};   ///< The whole posted slot, for reposting.
        std::uint64_t sequence{0};
        nbio::net::RdmaPacketType type{};
        std::uint64_t acknowledge{0};
    };

    /**
     * @brief A payload off the session and not yet asked for.
     */
    struct Held {
        std::span<char> payload{};
        std::span<char> chunk{};  ///< The whole slot, to be reposted when released.
        std::uint64_t sequence{0};
    };

    // Pulls the next posted receive and answers what arrived in it.
    nbio::async::Task<nbio::utility::expected<RdmaReceiveResult, std::string>> PullPacket();

    // Reads one packet off the session and says what was in it. The slot is not
    // reposted: it goes back when its payload does.
    nbio::async::Task<nbio::utility::expected<Incoming, std::string>> ReadPacket();

    // Works out what a packet meant: the ack inside it widens what may be sent, and
    // a payload goes on the queue for whoever asks for one.
    nbio::async::Task<nbio::utility::expected<void, std::string>> Absorb(Incoming packet);

    // One packet carrying `payload`, waited for: the header, the sequence, whatever
    // acknowledgement is owed, and the type the caller says it is.
    nbio::async::Task<nbio::utility::expected<void, std::string>> SendPacket(std::span<const char> payload,
                                                                             nbio::net::RdmaPacketType type);

    // Waits until the packets already in flight leave room for `packets` more.
    nbio::async::Task<nbio::utility::expected<void, std::string>> WaitForRoom(std::uint64_t packets);

    bool RoomFor(std::uint64_t packets) const noexcept;

    // Posts the slot a payload came in, which is also what lets the peer send more.
    nbio::utility::expected<void, std::string> Repost(std::span<char> chunk);

    // What start() runs: the reader's own loop.
    static nbio::async::Task<void> Read(std::shared_ptr<RdmaDeliverService> self);

    std::shared_ptr<RdmaSessionService> session_;
    Layout mine_;
    Layout peer_{};
    bool handshaken_{false};
    bool ended_{false};

    // This end's own numbering, and the two counters that decide whether it may send.
    std::uint64_t sent_{0};
    std::uint64_t acknowledged_{0};
    std::uint64_t taken_{0};
    std::uint64_t released_{0};

    // Payloads taken off the session and not yet asked for.
    std::deque<Held> ready_;
    std::uint64_t acknowledged_to_peer_{0};
    nbio::notification::ConditionVariable room_;
    nbio::notification::ConditionVariable ready_available_;

    // The receive slots, in posting order, and the sequence number of the next one
    // to pull. Posts and pulls are appended and consumed in one order, so the
    // cursor only ever counts up.
    std::vector<std::span<char>> receive_slots_;
    RdmaSeq next_receive_seq_{0};

    // The send slots, and the sequence number each last carried. A slot is only
    // reused after that number has retired, which is the whole of the send
    // lifetime rule.
    std::vector<std::span<char>> send_slots_;
    std::vector<RdmaSeq> send_slot_seq_;
    std::size_t send_cursor_{0};

    nbio::async::CoroutineJoinHandle reader_{};
};
}  // namespace nbio::net

#endif  // defined(NBIO_ENABLE_RDMA) && defined(__linux__)
