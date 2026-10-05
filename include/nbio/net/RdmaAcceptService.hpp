#pragma once

#if defined(NBIO_ENABLE_RDMA) && defined(__linux__)

#include <nbio/async/Task.hpp>
#include <nbio/net/Address.hpp>
#include <nbio/net/RdmaAcceptChannel.hpp>
#include <nbio/net/RdmaAcceptor.hpp>
#include <nbio/net/RdmaResourceManager.hpp>
#include <nbio/net/RdmaResult.hpp>
#include <nbio/net/RdmaSessionService.hpp>
#include <nbio/async/Runtime.hpp>
#include <memory>
#include <system_error>

namespace nbio::net {
class RdmaAcceptService final {
   public:
    RdmaAcceptService(RdmaResourceManager& resources, const Address& address, int backlog = 4096);

    RdmaAcceptService(const RdmaAcceptService&) = delete;
    RdmaAcceptService& operator=(const RdmaAcceptService&) = delete;
    RdmaAcceptService(RdmaAcceptService&&) = delete;
    RdmaAcceptService& operator=(RdmaAcceptService&&) = delete;

    ~RdmaAcceptService() noexcept = default;

    nbio::async::Task<RdmaResult<std::shared_ptr<RdmaSessionService>>> Accept();

    RdmaAcceptor& acceptor() noexcept { return acceptor_; }
    const RdmaAcceptor& acceptor() const noexcept { return acceptor_; }

   private:
    RdmaAcceptor acceptor_;
    RdmaAcceptChannel channel_;
};
}  // namespace nbio::net

#endif  // defined(NBIO_ENABLE_RDMA) && defined(__linux__)