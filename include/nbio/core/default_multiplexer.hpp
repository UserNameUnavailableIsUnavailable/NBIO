#pragma once

#include <nbio/core/epoll_multiplexer.hpp>
#include <nbio/core/io_uring_multiplexer.hpp>
#include <nbio/core/iocp_multiplexer.hpp>

namespace nbio::core {
#if defined(__linux__) && defined(NBIO_ENABLE_IO_URING)
using DefaultMultiplexer = IOURingMultiplexer;
#elif defined(__linux__)
using DefaultMultiplexer = IOURingMultiplexer;
#elif defined(_WIN32)
using DefaultMultiplexer = IOCPMultiplexer;
#endif
} // namespace nbio::core