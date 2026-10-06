#pragma once

#if defined(NBIO_ENABLE_RDMA) && defined(__linux__)

#include <memory>
#include <nbio/runtime/daemon.hpp>
#include <nbio/async/task.hpp>
#include <nbio/net/address.hpp>
#include <nbio/net/rdma_resource_manager.hpp>
#include <nbio/net/rdma_result.hpp>
#include <nbio/net/rdma_session_service.hpp>

namespace nbio::net {
class RdmaConnectService final {
   public:
    explicit RdmaConnectService(RdmaResourceManager& resources) noexcept : resources_(resources) {}

    RdmaConnectService(const RdmaConnectService&) = delete;
    RdmaConnectService& operator=(const RdmaConnectService&) = delete;
    RdmaConnectService(RdmaConnectService&&) = delete;
    RdmaConnectService& operator=(RdmaConnectService&&) = delete;

    ~RdmaConnectService() noexcept = default;

    nbio::async::Task<RdmaResult<std::shared_ptr<RdmaSessionService>>> Connect(const Address& peer);
    nbio::async::Task<RdmaResult<std::shared_ptr<RdmaSessionService>>> Connect(const Address& local,
                                                                               const Address& peer);

   private:
    RdmaResourceManager& resources_;
};
}  // namespace nbio::net

#endif  // defined(NBIO_ENABLE_RDMA) && defined(__linux__)