#if defined(NBIO_ENABLE_RDMA) && defined(__linux__)

#include <nbio/net/rdma_connect_service.hpp>
#include <nbio/runtime/daemon.hpp>

namespace nbio::net {
RdmaConnectService::RdmaConnectService(RdmaResource& resource, const net::Address& address)
    : connector_(resource),
      address_(address),
      connect_channel_(connector_, nbio::runtime::Daemon::multiplexer(), nbio::runtime::Daemon::scheduler()) {}

RdmaConnectService::~RdmaConnectService() noexcept = default;

RdmaResult<void> RdmaConnectService::Bind(const net::Address& source) noexcept { return connector_.Bind(source); }

nbio::async::Task<RdmaResult<RdmaCommunication>> RdmaConnectService::Connect() {
    co_return co_await connect_channel_.Connect(address_);
}
}  // namespace nbio::net

#endif  // defined(NBIO_ENABLE_RDMA) && defined(__linux__)
