#pragma once
#if defined(NBIO_ENABLE_RDMA) && defined(__linux__)

#include <nbio/net/rdma_connector.hpp>
#include <nbio/net/rdma_result.hpp>
#include <nbio/async/runtime.hpp>

#include "rdma_receive_channel.hpp"
#include "rdma_send_channel.hpp"

namespace nbio::net {
class Multiplexer;

class RdmaSessionService final {
   public:
    RdmaSessionService(nbio::net::RdmaConnector connection, nbio::core::Multiplexer& multiplexer,
                       nbio::async::Scheduler& scheduler);

    RdmaSessionService(const RdmaSessionService&) = delete;
    RdmaSessionService& operator=(const RdmaSessionService&) = delete;
    RdmaSessionService(RdmaSessionService&&) = delete;
    RdmaSessionService& operator=(RdmaSessionService&&) = delete;
    ~RdmaSessionService() noexcept = default;

    // Hands one acquired send chunk to the device and returns; PollSend() is
    // what waits for the completions that hand the chunks back.
    RdmaResult<void> Send(std::span<char> chunk, std::size_t length) noexcept;
    nbio::async::Task<RdmaResult<std::size_t>> PollSend(std::size_t count = 0);

    nbio::async::Task<RdmaResult<RdmaReceiveResult>> Receive();

    // A message that has already arrived, without waiting for one: nothing when
    // none is ready. For a coroutine that has something else to do meanwhile.
    nbio::async::Task<RdmaResult<RdmaReceiveResult>> TryReceive();

    // Hands a received chunk back for the next message.
    RdmaResult<void> Release(std::span<char> chunk) noexcept;

    nbio::net::RdmaConnector& connection() noexcept;
    const nbio::net::RdmaConnector& connection() const noexcept;

    RdmaSendChannel& send_channel() noexcept;
    const RdmaSendChannel& send_channel() const noexcept;

    RdmaReceiveChannel& receive_channel() noexcept;
    const RdmaReceiveChannel& receive_channel() const noexcept;

   private:
    nbio::net::RdmaConnector connection_;
    RdmaSendChannel send_channel_;
    RdmaReceiveChannel receive_channel_;
};
}  // namespace nbio::net

#endif  // defined(NBIO_ENABLE_RDMA) && defined(__linux__)
