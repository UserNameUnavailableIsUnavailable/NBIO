#pragma once

#if defined(NBIO_ENABLE_RDMA) && defined(__linux__)

#include <memory>
#include <nbio/runtime/daemon.hpp>
#include <nbio/async/task.hpp>
#include <nbio/net/address.hpp>
#include <nbio/net/rdma_listen_channel.hpp>
#include <nbio/net/rdma_listener.hpp>
#include <nbio/net/rdma_resource_manager.hpp>
#include <nbio/net/rdma_result.hpp>
#include <nbio/net/rdma_session_service.hpp>
#include <system_error>

namespace nbio::net {
class RdmaListenService final {
   public:
    RdmaListenService(RdmaResourceManager& resources, const Address& address, int backlog = 4096);

    RdmaListenService(const RdmaListenService&) = delete;
    RdmaListenService& operator=(const RdmaListenService&) = delete;
    RdmaListenService(RdmaListenService&&) = delete;
    RdmaListenService& operator=(RdmaListenService&&) = delete;

    ~RdmaListenService() noexcept = default;

    nbio::async::Task<RdmaResult<std::shared_ptr<RdmaSessionService>>> Accept();

    RdmaListener& listener() noexcept { return listener_; }
    const RdmaListener& listener() const noexcept { return listener_; }

   private:
    RdmaListener listener_;
    RdmaListenChannel channel_;
};
}  // namespace nbio::net

#endif  // defined(NBIO_ENABLE_RDMA) && defined(__linux__)