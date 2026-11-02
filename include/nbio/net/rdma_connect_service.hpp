#pragma once
#if defined(NBIO_ENABLE_RDMA) && defined(__linux__)

#include <nbio/async/task.hpp>
#include <nbio/net/rdma_communication.hpp>
#include <nbio/net/rdma_connect_channel.hpp>
#include <nbio/net/rdma_connector.hpp>
#include <nbio/net/rdma_resource.hpp>

namespace nbio::net {
/**
 * @brief The interface half of a connector: an optional bind, then one coroutine
 *        that resolves the destination and returns the prepared communication.
 */
class RdmaConnectService {
   public:
    RdmaConnectService(RdmaResource& resource, const net::Address& address);

    RdmaConnectService(const RdmaConnectService&) = delete;
    RdmaConnectService& operator=(const RdmaConnectService&) = delete;
    RdmaConnectService(RdmaConnectService&&) = delete;
    RdmaConnectService& operator=(RdmaConnectService&&) = delete;
    ~RdmaConnectService() noexcept;

    RdmaResult<void> Bind(const net::Address& source) noexcept;

    /** @brief Resolves and returns the prepared communication, not connected. */
    nbio::async::Task<RdmaResult<RdmaCommunication>> Connect();

   private:
    RdmaConnector connector_;
    net::Address address_;
    RdmaConnectChannel connect_channel_;
};
}  // namespace nbio::net

#endif  // defined(NBIO_ENABLE_RDMA) && defined(__linux__)
