#if defined(NBIO_ENABLE_RDMA) && defined(__linux__)

#include <nbio/net/RdmaConnectService.hpp>

#include <nbio/net/RdmaConnectChannel.hpp>
#include <nbio/net/RdmaConnector.hpp>
#include <new>
#include <utility>

namespace nbio::net {
namespace {
nbio::async::Task<nbio::Runtime, RdmaResult<std::shared_ptr<RdmaSessionService>>> ConnectOn(
    RdmaResourceManager& resources, const Address* local, const Address& peer) {
    RdmaConnector connector{resources};
    RdmaConnectChannel channel{connector, nbio::Runtime::multiplexer(), nbio::Runtime::scheduler()};
    if (local != nullptr) {
        if (auto bound = connector.Bind(*local); !bound) [[unlikely]] {
            co_return utility::unexpected(bound.error());
        }
    }
    if (auto connected = co_await channel.Connect(peer); !connected) [[unlikely]] {
        co_return utility::unexpected(connected.error());
    }
    try {
        co_return std::make_shared<RdmaSessionService>(std::move(connector), nbio::Runtime::multiplexer(),
                                                       nbio::Runtime::scheduler());
    } catch (const std::bad_alloc&) {
        co_return utility::unexpected(make_error_code(RdmaErrc::kResourceExhausted));
    } catch (...) {
        co_return utility::unexpected(make_error_code(RdmaErrc::kOperationFailed));
    }
}
}  // namespace

nbio::async::Task<nbio::Runtime, RdmaResult<std::shared_ptr<RdmaSessionService>>> RdmaConnectService::Connect(
    const Address& peer) {
    return ConnectOn(resources_, nullptr, peer);
}

nbio::async::Task<nbio::Runtime, RdmaResult<std::shared_ptr<RdmaSessionService>>> RdmaConnectService::Connect(
    const Address& local, const Address& peer) {
    return ConnectOn(resources_, &local, peer);
}
}  // namespace nbio::net

#endif  // defined(NBIO_ENABLE_RDMA) && defined(__linux__)