#pragma once
#if defined(NBIO_ENABLE_RDMA) && defined(__linux__)

#include <nbio/async/task.hpp>
#include <nbio/net/rdma_communication.hpp>
#include <nbio/net/rdma_listen_channel.hpp>
#include <nbio/net/rdma_listener.hpp>
#include <nbio/net/rdma_resource.hpp>

namespace nbio::net {
/**
 * @brief The interface half of a listener: the two phases of setup, then one
 *        coroutine that waits for a connection request and returns it prepared.
 */
class RdmaListenService {
   public:
    explicit RdmaListenService(RdmaResource& resource);

    RdmaListenService(const RdmaListenService&) = delete;
    RdmaListenService& operator=(const RdmaListenService&) = delete;
    RdmaListenService(RdmaListenService&&) = delete;
    RdmaListenService& operator=(RdmaListenService&&) = delete;
    ~RdmaListenService() noexcept;

    RdmaResult<void> Bind(const net::Address& address) noexcept;
    RdmaResult<void> Listen(int backlog = 4096) noexcept;

    /** @brief Returns the prepared communication, not accepted -- the caller may still refuse. */
    nbio::async::Task<RdmaResult<RdmaCommunication>> Accept();

    RdmaListener& listener() noexcept { return listener_; }

   private:
    RdmaListener listener_;
    RdmaListenChannel listen_channel_;
};
}  // namespace nbio::net

#endif  // defined(NBIO_ENABLE_RDMA) && defined(__linux__)
