#pragma once

#if defined(NBIO_ENABLE_RDMA) && defined(__linux__)

#include <cstddef>
#include <cstdint>
#include <nbio/utility/expected.hpp>
#include <span>
#include <string>
#include <system_error>

namespace nbio::net {
/**
 * @brief The sequence number a communication assigns to one submitted work request.
 *
 * @details Sequence numbers are assigned per direction and start at one, so zero
 * is never a valid sequence. They are what a completion comes back carrying, and
 * therefore what @ref RdmaCommunication::PeekSend and
 * @ref RdmaCommunication::PeekReceive are asked for. A sequence number is a handle
 * to one posted work request; it is not a cursor.
 */
using RdmaSeq = std::uint64_t;

enum class RdmaErrc {
    kOperationFailed = 1,
    kInvalidState,
    kResourceExhausted,
    kInvalidBuffer,
    kDeviceMismatch,
    kPeerClosed,
    kTimedOut,
    kUnexpectedEvent,
    kCompletionFailed,
    kNotReady,         ///< The work request has not been reaped yet.
    kInvalidSequence,  ///< Never posted, already released, or outside the live range.
    kQueueFull,        ///< The ring has no free slot; consume results to make room.
    kNoTarget,         ///< A connector has no target address.
};

/**
 * @brief The error category behind @ref RdmaErrc.
 */
class RdmaErrorCategory final : public std::error_category {
   public:
    const char* name() const noexcept override { return "nbio.rdma"; }

    std::string message(int value) const override {
        switch (static_cast<RdmaErrc>(value)) {
            case RdmaErrc::kOperationFailed:
                return "RDMA operation failed";
            case RdmaErrc::kInvalidState:
                return "RDMA connection is not in a valid state";
            case RdmaErrc::kResourceExhausted:
                return "RDMA resources are exhausted";
            case RdmaErrc::kInvalidBuffer:
                return "buffer does not belong to this RDMA connection";
            case RdmaErrc::kDeviceMismatch:
                return "RDMA operation resolved to a different device";
            case RdmaErrc::kPeerClosed:
                return "RDMA peer closed the connection";
            case RdmaErrc::kTimedOut:
                return "RDMA operation timed out";
            case RdmaErrc::kUnexpectedEvent:
                return "unexpected RDMA connection-management event";
            case RdmaErrc::kCompletionFailed:
                return "RDMA work completion failed";
            case RdmaErrc::kNotReady:
                return "RDMA work request has not completed yet";
            case RdmaErrc::kInvalidSequence:
                return "sequence number is not live on this direction";
            case RdmaErrc::kQueueFull:
                return "RDMA work request ring is full";
            case RdmaErrc::kNoTarget:
                return "RDMA connector has no target address";
        }
        return "unknown RDMA error";
    }
};

/**
 * @brief The single instance of @ref RdmaErrorCategory.
 */
inline const std::error_category& rdma_error_category() noexcept {
    static const RdmaErrorCategory category;
    return category;
}

/**
 * @brief Builds an @c std::error_code for an @ref RdmaErrc.
 */
inline std::error_code make_error_code(RdmaErrc error) noexcept {
    return {static_cast<int>(error), rdma_error_category()};
}

/**
 * @brief The result type of every RDMA operation.
 */
template <class T>
using RdmaResult = utility::expected<T, std::error_code>;

/**
 * @brief One retired send request, handed back to its poster.
 *
 * @details The buffer is exactly the span that was posted, so the caller can put
 * it back on a freelist, hand it to a pool, or simply write into it again. A send
 * completion's @c wc.byte_len is not meaningful across providers, which is why the
 * length is the one that was posted rather than one read from the completion.
 */
struct RdmaSendResult {
    RdmaSeq seq{0};          ///< The sequence number of the request.
    std::span<char> buffer{};  ///< The buffer as posted; it is the caller's again.
};

/**
 * @brief One retired receive request, handed back to its poster.
 *
 * @details @c buffer is the span that was posted, which is what the caller has to
 * reuse or free; @c bytes is what the peer actually wrote, so the payload is
 * <tt>buffer.first(bytes)</tt>. A receive may be shorter than the buffer, and a
 * caller that keeps only the trimmed span cannot give the buffer back.
 */
struct RdmaReceiveResult {
    RdmaSeq seq{0};            ///< The sequence number of the request.
    std::span<char> buffer{};  ///< The buffer as posted; it is the caller's again.
    std::size_t bytes{0};      ///< Bytes written by the peer; payload is @c buffer.first(bytes).
};

enum class RdmaPayloadState {
    kAvailable,
    kEnded,
};

struct RdmaPayloadResult {
    RdmaPayloadState state{RdmaPayloadState::kEnded};
    std::span<char> payload{};
};
}  // namespace nbio::net

#endif  // defined(NBIO_ENABLE_RDMA) && defined(__linux__)
