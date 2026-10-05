#pragma once

#if defined(NBIO_ENABLE_RDMA) && defined(__linux__)

#include <NBIO/Async/Task.hpp>
#include <NBIO/Net/Address.hpp>
#include <NBIO/Net/RdmaAcceptChannel.hpp>
#include <NBIO/Net/RdmaAcceptor.hpp>
#include <NBIO/Net/RdmaResourceManager.hpp>
#include <NBIO/Net/RdmaResult.hpp>
#include <NBIO/Net/RdmaSessionService.hpp>
#include <NBIO/Async/Runtime.hpp>
#include <memory>
#include <system_error>

namespace NBIO::Net {
class RdmaAcceptService final {
   public:
    RdmaAcceptService(RdmaResourceManager& resources, const Address& address, int backlog = 4096);

    RdmaAcceptService(const RdmaAcceptService&) = delete;
    RdmaAcceptService& operator=(const RdmaAcceptService&) = delete;
    RdmaAcceptService(RdmaAcceptService&&) = delete;
    RdmaAcceptService& operator=(RdmaAcceptService&&) = delete;

    ~RdmaAcceptService() noexcept = default;

    NBIO::Async::Task<RdmaResult<std::shared_ptr<RdmaSessionService>>> Accept();

    RdmaAcceptor& acceptor() noexcept { return acceptor_; }
    const RdmaAcceptor& acceptor() const noexcept { return acceptor_; }

   private:
    RdmaAcceptor acceptor_;
    RdmaAcceptChannel channel_;
};
}  // namespace NBIO::Net

#endif  // defined(NBIO_ENABLE_RDMA) && defined(__linux__)