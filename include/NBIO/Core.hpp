#pragma once

#if defined(__linux__)
namespace NBIO::Core {
class EpollMultiplexer;
} // namespace NBIO::Core
#include <NBIO/Core/Types.hpp>
namespace NBIO::Core {
} // namespace NBIO::Core
#endif // defined(__linux__)

#if defined(__linux__) && defined(NBIO_ENABLE_IO_URING)
namespace NBIO::Core {
class URingMultiplexer;
} // namespace NBIO::Core
#include <NBIO/Core/Types.hpp>
#endif // defined(__linux__) && defined(NBIO_ENABLE_IO_URING)