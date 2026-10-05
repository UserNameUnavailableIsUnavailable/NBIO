#pragma once

#if defined(NBIO_ENABLE_RDMA) && defined(__linux__)

#include <NBIO/Utility/Expected.hpp>
#include <span>
#include <string>
#include <system_error>

namespace NBIO::Net {
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
};

class RdmaErrorCategory final : public std::error_category {
   public:
    const char* name() const noexcept override { return "NBIO.rdma"; }

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
        }
        return "unknown RDMA error";
    }
};

inline const std::error_category& rdma_error_category() noexcept {
    static const RdmaErrorCategory category;
    return category;
}

inline std::error_code make_error_code(RdmaErrc error) noexcept {
    return {static_cast<int>(error), rdma_error_category()};
}

template <class T>
using RdmaResult = Utility::expected<T, std::error_code>;

enum class RdmaReceiveState {
    kData,
    kWouldBlock,
    kPeerClosed,
};

struct RdmaReceiveResult {
    RdmaReceiveState state{RdmaReceiveState::kWouldBlock};
    std::span<char> data{};
};

enum class RdmaBufferState {
    kAvailable,
    kWouldBlock,
};

struct RdmaBufferResult {
    RdmaBufferState state{RdmaBufferState::kWouldBlock};
    std::span<char> buffer{};
};

enum class RdmaPayloadState {
    kAvailable,
    kEnded,
};

struct RdmaPayloadResult {
    RdmaPayloadState state{RdmaPayloadState::kEnded};
    std::span<char> payload{};
};
}  // namespace NBIO::Net

#endif  // defined(NBIO_ENABLE_RDMA) && defined(__linux__)