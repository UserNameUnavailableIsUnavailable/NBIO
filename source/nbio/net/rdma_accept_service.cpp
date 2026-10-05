#if defined(NBIO_ENABLE_RDMA) && defined(__linux__)

#include <nbio/net/rdma_accept_service.hpp>

#include <utility>

namespace nbio::net {
RdmaAcceptService::RdmaAcceptService(RdmaResourceManager& resources, const Address& address, int backlog)
    : acceptor_(resources),
      channel_(acceptor_, nbio::async::Runtime::multiplexer(), nbio::async::Runtime::scheduler()) {
    if (auto reused = acceptor_.ReuseAddress(true); !reused) [[unlikely]] {
        throw std::system_error(reused.error(), "Failed to enable RDMA address reuse");
    }
    if (auto listening = acceptor_.Listen(address, backlog); !listening) [[unlikely]] {
        throw std::system_error(listening.error(), "Failed to listen on RDMA address");
    }
}

nbio::async::Task<RdmaResult<std::shared_ptr<RdmaSessionService>>> RdmaAcceptService::Accept() {
    auto accepted = co_await channel_.accept();
    if (!accepted) [[unlikely]] {
        co_return utility::unexpected(accepted.error());
    }
    try {
        co_return std::make_shared<RdmaSessionService>(std::move(*accepted), nbio::async::Runtime::multiplexer(),
                                                       nbio::async::Runtime::scheduler());
    } catch (const std::bad_alloc&) {
        co_return utility::unexpected(make_error_code(RdmaErrc::kResourceExhausted));
    } catch (...) {
        co_return utility::unexpected(make_error_code(RdmaErrc::kOperationFailed));
    }
}
}  // namespace nbio::net

#endif  // defined(NBIO_ENABLE_RDMA) && defined(__linux__)