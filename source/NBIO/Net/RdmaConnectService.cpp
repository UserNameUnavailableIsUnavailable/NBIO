#if defined(NBIO_ENABLE_RDMA) && defined(__linux__)

#include <NBIO/Net/RdmaConnectService.hpp>

#include <NBIO/Net/RdmaConnectChannel.hpp>
#include <NBIO/Net/RdmaConnector.hpp>
#include <new>
#include <utility>

namespace NBIO::Net {
namespace {
NBIO::Async::Task<RdmaResult<std::shared_ptr<RdmaSessionService>>> ConnectOn(
    RdmaResourceManager& resources, const Address* local, const Address& peer) {
    RdmaConnector connector{resources};
    RdmaConnectChannel channel{connector, NBIO::Async::Runtime::multiplexer(), NBIO::Async::Runtime::scheduler()};
    if (local != nullptr) {
        if (auto bound = connector.Bind(*local); !bound) [[unlikely]] {
            co_return Utility::unexpected(bound.error());
        }
    }
    if (auto connected = co_await channel.Connect(peer); !connected) [[unlikely]] {
        co_return Utility::unexpected(connected.error());
    }
    try {
        co_return std::make_shared<RdmaSessionService>(std::move(connector), NBIO::Async::Runtime::multiplexer(),
                                                       NBIO::Async::Runtime::scheduler());
    } catch (const std::bad_alloc&) {
        co_return Utility::unexpected(make_error_code(RdmaErrc::kResourceExhausted));
    } catch (...) {
        co_return Utility::unexpected(make_error_code(RdmaErrc::kOperationFailed));
    }
}
}  // namespace

NBIO::Async::Task<RdmaResult<std::shared_ptr<RdmaSessionService>>> RdmaConnectService::Connect(
    const Address& peer) {
    return ConnectOn(resources_, nullptr, peer);
}

NBIO::Async::Task<RdmaResult<std::shared_ptr<RdmaSessionService>>> RdmaConnectService::Connect(
    const Address& local, const Address& peer) {
    return ConnectOn(resources_, &local, peer);
}
}  // namespace NBIO::Net

#endif  // defined(NBIO_ENABLE_RDMA) && defined(__linux__)