#pragma once

namespace nbio::core {
enum class MultiplexerType {
#if defined(__linux__)
    kEpoll,
#if defined(NBIO_ENABLE_IO_URING)
    kURing
#endif
#elif defined(WIN32)
    kIOCP
#endif
};

// Every channel is simplex: it is dedicated to exactly one event. A connection
// therefore owns two channels (receive and send) over the same socket, and a
// listener owns one (accept).
enum class ChannelType {
    kAccept,
    // A connection being made: the socket becoming writable is the completion, and
    // what it means is the socket's own answer rather than the readiness itself.
    kConnect,
    kReceive,
    kSend,
    kRead,
    kWrite,
    kTimer,
    kSystemSignal,
    kNotify,
#if defined(NBIO_ENABLE_RDMA)
    kRdmaAccept,
    kRdmaConnect,
    kRdmaSend,
    kRdmaReceive,
#endif
};
}  // namespace nbio::core
