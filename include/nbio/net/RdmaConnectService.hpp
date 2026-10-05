#pragma once

#if defined(NBIO_ENABLE_RDMA) && defined(__linux__)

#include <nbio/async/Task.hpp>
#include <nbio/net/Address.hpp>
#include <nbio/net/RdmaResourceManager.hpp>
#include <nbio/net/RdmaResult.hpp>
#include <nbio/net/RdmaSessionService.hpp>
#include <nbio/runtime/Runtime.hpp>
#include <memory>

namespace nbio::net {
class RdmaConnectService final {
   public:
    explicit RdmaConnectService(RdmaResourceManager& resources) noexcept : resources_(resources) {}

    RdmaConnectService(const RdmaConnectService&) = delete;
    RdmaConnectService& operator=(const RdmaConnectService&) = delete;
    RdmaConnectService(RdmaConnectService&&) = delete;
    RdmaConnectService& operator=(RdmaConnectService&&) = delete;

    ~RdmaConnectService() noexcept = default;

    nbio::async::Task<nbio::Runtime, RdmaResult<std::shared_ptr<RdmaSessionService>>> Connect(const Address& peer);
    nbio::async::Task<nbio::Runtime, RdmaResult<std::shared_ptr<RdmaSessionService>>> Connect(const Address& local,
                                                                                             const Address& peer);

   private:
    RdmaResourceManager& resources_;
};
}  // namespace nbio::net

#endif  // defined(NBIO_ENABLE_RDMA) && defined(__linux__)