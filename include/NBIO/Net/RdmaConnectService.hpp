#pragma once

#if defined(NBIO_ENABLE_RDMA) && defined(__linux__)

#include <NBIO/Async/Task.hpp>
#include <NBIO/Net/Address.hpp>
#include <NBIO/Net/RdmaResourceManager.hpp>
#include <NBIO/Net/RdmaResult.hpp>
#include <NBIO/Net/RdmaSessionService.hpp>
#include <NBIO/Async/Runtime.hpp>
#include <memory>

namespace NBIO::Net {
class RdmaConnectService final {
   public:
    explicit RdmaConnectService(RdmaResourceManager& resources) noexcept : resources_(resources) {}

    RdmaConnectService(const RdmaConnectService&) = delete;
    RdmaConnectService& operator=(const RdmaConnectService&) = delete;
    RdmaConnectService(RdmaConnectService&&) = delete;
    RdmaConnectService& operator=(RdmaConnectService&&) = delete;

    ~RdmaConnectService() noexcept = default;

    NBIO::Async::Task<RdmaResult<std::shared_ptr<RdmaSessionService>>> Connect(const Address& peer);
    NBIO::Async::Task<RdmaResult<std::shared_ptr<RdmaSessionService>>> Connect(const Address& local,
                                                                                             const Address& peer);

   private:
    RdmaResourceManager& resources_;
};
}  // namespace NBIO::Net

#endif  // defined(NBIO_ENABLE_RDMA) && defined(__linux__)