#pragma once

#include <nbio/core/channel.hpp>
#include <nbio/core/multiplexer.hpp>
#include <nbio/core/types.hpp>

#if defined(__linux__)
#include <nbio/core/epoll_multiplexer.hpp>
#if defined(NBIO_ENABLE_IO_URING)
#include <nbio/core/uring_multiplexer.hpp>
#endif
#endif