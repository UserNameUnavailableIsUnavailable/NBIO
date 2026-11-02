#if defined(NBIO_ENABLE_RDMA) && defined(__linux__)

#include <algorithm>
#include <array>
#include <cstring>
#include <nbio/async.hpp>
#include <nbio/net/rdma_deliver_service.hpp>
#include <nbio/runtime/daemon.hpp>
#include <nbio/utility/byte.hpp>
#include <stdexcept>
#include <utility>

namespace nbio::net {
namespace {

// Every packet carries the acknowledgement it knows, so this only decides how often a
// receiver that is sending nothing of its own says something: a quarter of the window is
// often enough that a sender never runs out of room, and rare enough that the
// acknowledgements are not the traffic.
std::uint64_t AckEvery(std::uint64_t chunk_count) noexcept { return std::max<std::uint64_t>(1, chunk_count / 4); }

constexpr std::size_t kMetaBytes = 2 * sizeof(std::uint64_t);

std::array<char, kMetaBytes> EncodeLayout(const RdmaDeliverService::Layout& layout) noexcept {
    std::array<char, kMetaBytes> bytes{};
    const auto size = nbio::utility::ToBigEndian(layout.chunk_size);
    const auto count = nbio::utility::ToBigEndian(layout.chunk_count);
    std::memcpy(bytes.data(), &size, sizeof(size));
    std::memcpy(bytes.data() + sizeof(size), &count, sizeof(count));
    return bytes;
}

bool DecodeLayout(std::span<const char> bytes, RdmaDeliverService::Layout& layout) noexcept {
    if (bytes.size() < kMetaBytes) {
        return false;
    }
    std::uint64_t size = 0;
    std::uint64_t count = 0;
    std::memcpy(&size, bytes.data(), sizeof(size));
    std::memcpy(&count, bytes.data() + sizeof(size), sizeof(count));
    layout.chunk_size = nbio::utility::FromBigEndian(size);
    layout.chunk_count = nbio::utility::FromBigEndian(count);
    return layout.payload_size() > 0 && layout.chunk_count > 0;
}

const nbio::net::RdmaHeader& AsPacket(std::span<char> packet) noexcept {
    return *reinterpret_cast<const nbio::net::RdmaHeader*>(packet.data());
}
}  // namespace

RdmaDeliverService::RdmaDeliverService(std::shared_ptr<RdmaSessionService> session, Layout layout)
    : session_(std::move(session)), mine_(layout) {
    if (session_ == nullptr) [[unlikely]] {
        throw std::invalid_argument("an rdma deliver service needs a session");
    }
    if (mine_.payload_size() == 0 || mine_.chunk_count == 0) [[unlikely]] {
        throw std::invalid_argument("an rdma deliver service needs slots that hold a packet and a payload");
    }

    const auto receive_depth = session_->communication().options().receive_queue_depth;
    if (mine_.chunk_count > receive_depth) [[unlikely]] {
        throw std::invalid_argument("the layout offers " + std::to_string(mine_.chunk_count) +
                                    " slots where the connection posts receives for only " +
                                    std::to_string(receive_depth));
    }

    const std::span<char> receive_region = session_->communication().resource().receive_memory_region();
    const std::span<char> send_region = session_->communication().resource().send_memory();
    const auto chunk_size = static_cast<std::size_t>(mine_.chunk_size);
    if (receive_region.size() / chunk_size < mine_.chunk_count) [[unlikely]] {
        throw std::invalid_argument("the receive region does not hold the layout's slots");
    }
    if (send_region.size() < chunk_size) [[unlikely]] {
        throw std::invalid_argument("the send region does not hold one packet");
    }

    receive_slots_.reserve(static_cast<std::size_t>(mine_.chunk_count));
    for (std::uint64_t index = 0; index < mine_.chunk_count; ++index) {
        receive_slots_.push_back(receive_region.subspan(static_cast<std::size_t>(index) * chunk_size, chunk_size));
    }
    const std::size_t send_slot_count = std::min(send_region.size() / chunk_size,
                                                 static_cast<std::size_t>(session_->communication().options().send_queue_depth));
    send_slots_.reserve(send_slot_count);
    for (std::size_t index = 0; index < send_slot_count; ++index) {
        send_slots_.push_back(send_region.subspan(index * chunk_size, chunk_size));
    }
    send_slot_seq_.assign(send_slots_.size(), 0);

    // Every receive is posted before the handshake can read anything.
    for (const auto slot : receive_slots_) {
        auto posted = session_->SubmitReceive(slot);
        if (!posted) [[unlikely]] {
            throw std::runtime_error("failed to post a receive: " + posted.error().message());
        }
        if (next_receive_seq_ == 0) {
            next_receive_seq_ = *posted;
        }
    }
}

RdmaDeliverService::~RdmaDeliverService() noexcept = default;

RdmaResult<void> RdmaDeliverService::Repost(std::span<char> chunk) noexcept {
    auto posted = session_->SubmitReceive(chunk);
    if (!posted) [[unlikely]] {
        return nbio::utility::unexpected(posted.error());
    }
    return {};
}

nbio::async::Task<RdmaResult<RdmaReceiveResult>> RdmaDeliverService::PullPacket() {
    auto waited = co_await session_->WaitReceive(next_receive_seq_);
    if (!waited) [[unlikely]] {
        co_return nbio::utility::unexpected(waited.error());
    }
    auto peeked = session_->PeekReceive(next_receive_seq_);
    if (!peeked) [[unlikely]] {
        co_return nbio::utility::unexpected(peeked.error());
    }
    session_->CompleteReceive(next_receive_seq_);
    ++next_receive_seq_;
    co_return *peeked;
}

nbio::async::Task<RdmaResult<void>> RdmaDeliverService::handshake() {
    const auto mine = EncodeLayout(mine_);
    const auto open = nbio::net::RdmaPacketType::kMeta | nbio::net::RdmaPacketType::kAck;
    if (auto sent = co_await SendPacket(std::span<const char>(mine.data(), mine.size()), open); !sent) [[unlikely]] {
        co_return nbio::utility::unexpected(sent.error());
    }

    auto incoming = co_await ReadPacket();
    if (!incoming) [[unlikely]] {
        co_return nbio::utility::unexpected(incoming.error());
    }
    if (!(incoming->type & nbio::net::RdmaPacketType::kMeta)) [[unlikely]] {
        co_return nbio::utility::unexpected(make_error_code(RdmaErrc::kUnexpectedEvent));
    }
    if (!DecodeLayout(incoming->packet.subspan(sizeof(nbio::net::RdmaHeader)), peer_)) [[unlikely]] {
        co_return nbio::utility::unexpected(make_error_code(RdmaErrc::kOperationFailed));
    }
    if (auto released = Repost(incoming->chunk); !released) [[unlikely]] {
        co_return nbio::utility::unexpected(released.error());
    }
    taken_ = 1;
    released_ = 1;
    acknowledged_to_peer_ = 1;
    handshaken_ = true;
    co_return RdmaResult<void>{};
}

void RdmaDeliverService::Start() {
    reader_ = nbio::async::Spawn(Read(shared_from_this()));
}

nbio::async::Task<void> RdmaDeliverService::Read(std::shared_ptr<RdmaDeliverService> self) {
    while (!self->ended_) {
        auto packet = co_await self->ReadPacket();
        if (!packet) [[unlikely]] {
            self->ended_ = true;
            self->room_.NotifyAll();
            self->ready_available_.NotifyAll();
            co_return;
        }
        if (auto absorbed = co_await self->Absorb(std::move(*packet)); !absorbed) [[unlikely]] {
            self->ended_ = true;
            self->room_.NotifyAll();
            self->ready_available_.NotifyAll();
            co_return;
        }
    }
}

void RdmaDeliverService::Stop() noexcept {
    ended_ = true;
    reader_.Cancel();
    room_.NotifyAll();
    ready_available_.NotifyAll();
}

nbio::async::Task<RdmaResult<RdmaDeliverService::Incoming>> RdmaDeliverService::ReadPacket() {
    auto pulled = co_await PullPacket();
    if (!pulled) [[unlikely]] {
        co_return nbio::utility::unexpected(pulled.error());
    }

    const std::span<char> chunk = pulled->buffer;
    const std::span<char> packet = chunk.first(std::min<std::size_t>(pulled->bytes, chunk.size()));
    if (packet.size() < sizeof(nbio::net::RdmaHeader)) [[unlikely]] {
        (void)Repost(chunk);
        co_return nbio::utility::unexpected(make_error_code(RdmaErrc::kOperationFailed));
    }

    const auto& header = AsPacket(packet);
    co_return Incoming{.packet = packet,
                       .chunk = chunk,
                       .sequence = header.sequence,
                       .type = header.type,
                       .acknowledge = header.acknowledge};
}

nbio::async::Task<RdmaResult<void>> RdmaDeliverService::Absorb(Incoming packet) {
    taken_ = std::max(taken_, packet.sequence);
    if (packet.type & nbio::net::RdmaPacketType::kAck) {
        acknowledged_ = std::max(acknowledged_, packet.acknowledge);
        room_.NotifyAll();
    }

    if (!(packet.type & nbio::net::RdmaPacketType::kPayload)) {
        if (auto released = Repost(packet.chunk); !released) [[unlikely]] {
            co_return nbio::utility::unexpected(released.error());
        }
        released_ = std::max(released_, packet.sequence);
        co_return RdmaResult<void>{};
    }

    ready_.push_back(Held{.payload = packet.packet.subspan(sizeof(nbio::net::RdmaHeader)),
                          .chunk = packet.chunk,
                          .sequence = packet.sequence});
    ready_available_.NotifyOne();
    co_return RdmaResult<void>{};
}

nbio::async::Task<RdmaResult<void>> RdmaDeliverService::Send(std::span<const char> payload) {
    if (!handshaken_) [[unlikely]] {
        co_return nbio::utility::unexpected(make_error_code(RdmaErrc::kInvalidState));
    }

    const auto width = std::min(mine_.payload_size(), peer_.payload_size());
    std::size_t offset = 0;
    while (offset < payload.size()) {
        const auto take = static_cast<std::size_t>(std::min<std::uint64_t>(width, payload.size() - offset));
        if (auto sent = co_await SendPacket(payload.subspan(offset, take),
                                            nbio::net::RdmaPacketType::kPayload | nbio::net::RdmaPacketType::kAck);
            !sent) [[unlikely]] {
            co_return nbio::utility::unexpected(sent.error());
        }
        offset += take;
    }
    co_return RdmaResult<void>{};
}

nbio::async::Task<RdmaResult<void>> RdmaDeliverService::SendPacket(std::span<const char> payload,
                                                                   nbio::net::RdmaPacketType type) {
    if (auto room = co_await WaitForRoom(1); !room) [[unlikely]] {
        co_return nbio::utility::unexpected(room.error());
    }
    if (send_slots_.empty()) [[unlikely]] {
        co_return nbio::utility::unexpected(make_error_code(RdmaErrc::kResourceExhausted));
    }

    const std::size_t index = send_cursor_;
    if (send_slot_seq_[index] != 0) {
        auto retired = co_await session_->WaitSend(send_slot_seq_[index]);
        if (!retired) [[unlikely]] {
            co_return nbio::utility::unexpected(retired.error());
        }
        session_->CompleteSend(send_slot_seq_[index]);
        send_slot_seq_[index] = 0;
    }

    const std::span<char> slot = send_slots_[index];
    if (payload.size() + sizeof(nbio::net::RdmaHeader) > slot.size()) [[unlikely]] {
        co_return nbio::utility::unexpected(make_error_code(RdmaErrc::kInvalidBuffer));
    }

    nbio::net::RdmaHeader header{};
    header.sequence = ++sent_;
    header.acknowledge = released_;
    header.type = type;
    std::memcpy(slot.data(), &header, sizeof(header));
    if (!payload.empty()) {
        std::memcpy(slot.data() + sizeof(header), payload.data(), payload.size());
    }
    acknowledged_to_peer_ = std::max(acknowledged_to_peer_, header.acknowledge);
    room_.NotifyOne();

    const auto used = slot.first(sizeof(header) + payload.size());
    auto posted = session_->SubmitSend(used);
    if (!posted) [[unlikely]] {
        co_return nbio::utility::unexpected(posted.error());
    }
    send_slot_seq_[index] = *posted;
    send_cursor_ = (index + 1) % send_slots_.size();
    co_return RdmaResult<void>{};
}

nbio::async::Task<RdmaResult<RdmaPayloadResult>> RdmaDeliverService::Receive() {
    co_await ready_available_.wait([this] { return !ready_.empty() || ended_; });
    if (ready_.empty()) [[unlikely]] {
        co_return RdmaPayloadResult{.state = RdmaPayloadState::kEnded};
    }
    co_return RdmaPayloadResult{.state = RdmaPayloadState::kAvailable, .payload = ready_.front().payload};
}

nbio::async::Task<RdmaResult<void>> RdmaDeliverService::release(std::span<char> payload) {
    if (ready_.empty() || ready_.front().payload.data() != payload.data()) [[unlikely]] {
        co_return nbio::utility::unexpected(make_error_code(RdmaErrc::kInvalidBuffer));
    }
    const Held held = ready_.front();
    ready_.pop_front();

    if (auto released = Repost(held.chunk); !released) [[unlikely]] {
        co_return nbio::utility::unexpected(released.error());
    }
    released_ = std::max(released_, held.sequence);

    if (released_ > acknowledged_to_peer_ && released_ - acknowledged_to_peer_ >= AckEvery(mine_.chunk_count)) {
        if (auto sent = co_await SendPacket(std::span<const char>{}, nbio::net::RdmaPacketType::kAck); !sent) [[unlikely]] {
            co_return nbio::utility::unexpected(sent.error());
        }
    }
    co_return RdmaResult<void>{};
}

bool RdmaDeliverService::RoomFor(std::uint64_t packets) const noexcept {
    if (!handshaken_) {
        return sent_ - acknowledged_ + packets <= 1;
    }
    return sent_ - acknowledged_ + packets <= peer_.chunk_count;
}

nbio::async::Task<RdmaResult<void>> RdmaDeliverService::WaitForRoom(std::uint64_t packets) {
    co_await room_.wait([this, packets] { return ended_ || RoomFor(packets); });
    if (ended_ && !RoomFor(packets)) [[unlikely]] {
        co_return nbio::utility::unexpected(make_error_code(RdmaErrc::kPeerClosed));
    }
    co_return RdmaResult<void>{};
}
}  // namespace nbio::net

#endif  // defined(NBIO_ENABLE_RDMA) && defined(__linux__)
