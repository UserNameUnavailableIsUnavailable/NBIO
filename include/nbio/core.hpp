#pragma once

#if defined(__linux__)
namespace nbio::core {
class EpollMultiplexer;
} // namespace nbio::core
#include <nbio/core/types.hpp>
namespace nbio::core {
} // namespace nbio::core
#endif // defined(__linux__)

#if defined(__linux__) && defined(NBIO_ENABLE_IO_URING)
namespace nbio::core {
class URingMultiplexer;
} // namespace nbio::core
#include <nbio/core/types.hpp>
#endif // defined(__linux__) && defined(NBIO_ENABLE_IO_URING)