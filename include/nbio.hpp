#pragma once

#include <async/Async.hpp>
#include <async/Coroutine.hpp>
#include <net/Address.hpp>
#include <net/TcpSocket.hpp>
#include <runtime/Runtime.hpp>
#include <core/EpollMultiplexer.hpp>
#include <core/Multiplexer.hpp>
#include <runtime/Runtime.hpp>
#include <net/TcpConnectChannel.hpp>
#include <memory>

#if defined(NBIO_ENABLE_IO_URING)
#include <core/URingMultiplexer.hpp>
#endif

#if defined(NBIO_ENABLE_RDMA)
#include <net/RdmaAcceptChannel.hpp>
#include <net/RdmaConnectChannel.hpp>
#include <net/RdmaDeliverService.hpp>
#include <net/RdmaSessionService.hpp>
#endif

#include <fs/FileStream.hpp>
#include <fs/FileStreamService.hpp>
#include <signal/SystemSignalService.hpp>
#include <time/SystemTimeService.hpp>
#include <net/TcpAcceptService.hpp>
#include <net/TcpConnectService.hpp>
#include <net/TcpSessionService.hpp>

// Umbrella header for the nbio backend: the non-blocking I/O runtime built on
// epoll / io_uring. Everything here is backend-specific; the generic coroutine
// machinery lives in nbio::async.
namespace nbio {
template <typename T>
using Task = nbio::async::Task<nbio::runtime, T>;

inline bool is_initialized() { return nbio::runtime::is_initialized(); }

inline void initialize(std::unique_ptr<core::Multiplexer> multiplexer) {
    nbio::runtime::initialize(std::move(multiplexer));
}

inline void run(Task<void> main) { nbio::async::run(std::move(main)); }

template <typename T>
nbio::async::CoroutineToken Spawn(Task<T> task) {
    return nbio::async::Spawn(std::move(task));
}
}  // namespace nbio
