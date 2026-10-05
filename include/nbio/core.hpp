#pragma once

#if defined(__linux__)
namespace nbio::Core {
class EpollMultiplexer;
} // namespace nbio::Core
#include <nbio/core/Types.hpp>
namespace nbio::Core {
} // namespace nbio::Core
#endif // defined(__linux__)

#if defined(__linux__) && defined(NBIO_ENABLE_IO_URING)
namespace nbio::Core {
class URingMultiplexer;
} // namespace nbio::Core
#include <nbio/core/Types.hpp>
#endif // defined(__linux__) && defined(NBIO_ENABLE_IO_URING)