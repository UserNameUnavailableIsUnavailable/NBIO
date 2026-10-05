#pragma once

#if defined(NBIO_ENABLE_RDMA) && defined(__linux__)

#include <nbio/async/task.hpp>
#include <nbio/net/address.hpp>
#include <nbio/net/rdma_accept_channel.hpp>
#include <nbio/net/rdma_acceptor.hpp>
#include <nbio/net/rdma_resource_manager.hpp>
#include <nbio/net/rdma_result.hpp>
#include <nbio/net/rdma_session_service.hpp>
#include <nbio/async/runtime.hpp>
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