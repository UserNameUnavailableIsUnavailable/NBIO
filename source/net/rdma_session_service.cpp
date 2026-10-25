#if defined(NBIO_ENABLE_RDMA) && defined(__linux__)

#include <nbio/net/rdma_session_service.hpp>
#include <utility>

namespace nbio::net {
RdmaSessionService::RdmaSessionService(nbio::net::RdmaConnector connection, nbio::core::Multiplexer& multiplexer,
                                       nbio::async::Scheduler& scheduler)
    : connection_(std::move(connection)),
      send_channel_(connection_, multiplexer, scheduler),
      receive_channel_(connection_, multiplexer, scheduler),
      connect_channel_(connection_, multiplexer, scheduler) {}

nbio::async::Task<RdmaResult<void>> RdmaSessionService::Establish() { return connect_channel_.Establish(); }
}  // namespace nbio::net

#endif  // defined(NBIO_ENABLE_RDMA) && defined(__linux__)
