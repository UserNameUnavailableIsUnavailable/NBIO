#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <system_error>

namespace nbio::fs {
enum class OperationStatus {
    kDone,
    kPending,
    kError,
};

struct Transmission {
    OperationStatus status{OperationStatus::kPending};
    std::span<char> buffer;
    std::size_t bytes{0};
    std::uint64_t offset{0};
    std::error_code error_code{};
};

namespace detail {
inline bool ValidFileRange(std::uint64_t offset, std::size_t size) noexcept {
    constexpr auto maximum = static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
    return offset <= maximum && size <= maximum - offset;
}
}  // namespace detail
}  // namespace nbio::fs