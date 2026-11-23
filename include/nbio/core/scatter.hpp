#pragma once

#include <cstddef>

namespace nbio::core {
// Describes bytes, not native iovec/WSABUF layout. Storage must outlive the I/O.
struct Scatter {
    void* buffers;
    std::size_t count;
};
}  // namespace nbio::core
