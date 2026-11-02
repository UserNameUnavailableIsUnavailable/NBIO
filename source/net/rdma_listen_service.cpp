#if defined(NBIO_ENABLE_RDMA) && defined(__linux__)

#include <nbio/net/rdma_listen_service.hpp>
#include <nbio/runtime/daemon.hpp>

namespace nbio::net {
RdmaListenService::RdmaListenService(RdmaResource& resource)
    : listener_(resource),
      listen_channel_(listener_, nbio::runtime::Daemon::multiplexer(), nbio::runtime::Daemon::scheduler()) {}

RdmaListenService::~RdmaListenService() noexcept = default;

RdmaResult<void> RdmaListenService::Bind(const net::Address& address) noexcept { return listener_.Bind(address); }

RdmaResult<void> RdmaListenService::Listen(int backlog) noexcept { return listener_.Listen(backlog); }

nbio::async::Task<RdmaResult<RdmaCommunication>> RdmaListenService::Accept() {
    co_return co_await listen_channel_.Accept();
}
}  // namespace nbio::net

#endif  // defined(NBIO_ENABLE_RDMA) && defined(__linux__)
