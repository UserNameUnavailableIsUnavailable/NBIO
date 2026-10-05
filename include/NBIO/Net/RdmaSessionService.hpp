#pragma once
#if defined(NBIO_ENABLE_RDMA) && defined(__linux__)

#include <NBIO/Net/RdmaConnector.hpp>
#include <NBIO/Net/RdmaResult.hpp>
#include <NBIO/Async/Runtime.hpp>

#include "RdmaReceiveChannel.hpp"
#include "RdmaSendChannel.hpp"

namespace NBIO::Net {
class Multiplexer;

class RdmaSessionService final {
   public:
    RdmaSessionService(NBIO::Net::RdmaConnector connection, NBIO::Core::Multiplexer& multiplexer,
                       NBIO::Async::Scheduler& scheduler);

    RdmaSessionService(const RdmaSessionService&) = delete;
    RdmaSessionService& operator=(const RdmaSessionService&) = delete;
    RdmaSessionService(RdmaSessionService&&) = delete;
    RdmaSessionService& operator=(RdmaSessionService&&) = delete;
    ~RdmaSessionService() noexcept = default;

    // Hands one acquired send chunk to the device and returns; PollSend() is
    // what waits for the completions that hand the chunks back.
    RdmaResult<void> Send(std::span<char> chunk, std::size_t length) noexcept;
    NBIO::Async::Task<RdmaResult<std::size_t>> PollSend(std::size_t count = 0);

    NBIO::Async::Task<RdmaResult<RdmaReceiveResult>> Receive();

    // A message that has already arrived, without waiting for one: nothing when
    // none is ready. For a coroutine that has something else to do meanwhile.
    NBIO::Async::Task<RdmaResult<RdmaReceiveResult>> TryReceive();

    // Hands a received chunk back for the next message.
    RdmaResult<void> Release(std::span<char> chunk) noexcept;

    NBIO::Net::RdmaConnector& connection() noexcept;
    const NBIO::Net::RdmaConnector& connection() const noexcept;

    RdmaSendChannel& send_channel() noexcept;
    const RdmaSendChannel& send_channel() const noexcept;

    RdmaReceiveChannel& receive_channel() noexcept;
    const RdmaReceiveChannel& receive_channel() const noexcept;

   private:
    NBIO::Net::RdmaConnector connection_;
    RdmaSendChannel send_channel_;
    RdmaReceiveChannel receive_channel_;
};
}  // namespace NBIO::Net

#endif  // defined(NBIO_ENABLE_RDMA) && defined(__linux__)
