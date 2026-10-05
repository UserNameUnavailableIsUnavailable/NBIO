#pragma once

#include <nbio/async/Async.hpp>
#include <nbio/async/Coroutine.hpp>
#include <nbio/net/Address.hpp>
#include <nbio/net/TcpSocket.hpp>
#include <nbio/runtime/Runtime.hpp>
#include <nbio/core/EpollMultiplexer.hpp>
#include <nbio/core/Multiplexer.hpp>
#include <nbio/runtime/Runtime.hpp>
#include <nbio/net/TcpConnectChannel.hpp>
#include <memory>

#if defined(NBIO_ENABLE_IO_URING)
#include <nbio/core/URingMultiplexer.hpp>
#endif

#if defined(NBIO_ENABLE_RDMA)
#include <nbio/net/RdmaAcceptChannel.hpp>
#include <nbio/net/RdmaAcceptService.hpp>
#include <nbio/net/RdmaConnectChannel.hpp>
#include <nbio/net/RdmaConnectService.hpp>
#include <nbio/net/RdmaDeliverService.hpp>
#include <nbio/net/RdmaSessionService.hpp>
#endif

#include <nbio/fs/FileStream.hpp>
#include <nbio/fs/FileStreamService.hpp>
#include <nbio/signal/SystemSignalService.hpp>
#include <nbio/time/SystemTimeService.hpp>
#include <nbio/net/TcpAcceptService.hpp>
#include <nbio/net/TcpConnectService.hpp>
#include <nbio/net/TcpSessionService.hpp>

namespace nbio {
inline bool is_initialized() {
    return nbio::Runtime::is_initialized();
}

inline void initialize(std::unique_ptr<core::Multiplexer> multiplexer) {
    nbio::Runtime::initialize(std::move(multiplexer));
}
template <typename T>
using Task = nbio::async::Task<nbio::Runtime, T>;

inline void Run(Task<void> main) {
    async::Run(std::move(main));
}

template <typename T>
inline nbio::async::CoroutineToken Spawn(Task<T> task) {
    return async::Spawn(std::move(task));
}
}  // namespace nbio
