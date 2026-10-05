#include <NBIO/Net/TcpReceiveChannel.hpp>

#include <NBIO/Async/Coroutine.hpp>
#include <NBIO/Net/TcpConnector.hpp>

#include <NBIO/Async/Runtime.hpp>
#include <span>
#include <stdexcept>
#include <utility>

namespace NBIO::Net {
class ReceiveAwaiter {
   public:
    ReceiveAwaiter(TcpReceiveChannel& channel, std::span<char> buffer) : channel_(channel), buffer_(buffer) {}

    ReceiveAwaiter(const ReceiveAwaiter&) = delete;
    ReceiveAwaiter& operator=(const ReceiveAwaiter&) = delete;

    // No cancellation hook. The Parked Coroutine keeps this frame's control block
    // alive, and the channel owns the waiter until it answers it, so a stale
    // registration is impossible rather than detected.

    bool await_ready() const noexcept { return false; }

    template <typename PromiseType>
    void await_suspend(std::coroutine_handle<PromiseType> handle) noexcept {
        transmission_.status = OperationStatus::kPending;
        transmission_.bytes = 0;
        transmission_.error_code = {};
        transmission_.buffer = buffer_;
        channel_.Prepare(Async::Coroutine::FromHandle(handle), &transmission_);
        channel_.Arm();
    }

    Net::Transmission await_resume() noexcept { return transmission_; }

   private:
    TcpReceiveChannel& channel_;
    std::span<char> buffer_;
    Net::Transmission transmission_{};
};

TcpReceiveChannel::TcpReceiveChannel(NBIO::Net::TcpConnector& connector,
                                     NBIO::Core::Multiplexer& multiplexer,
                                     NBIO::Async::Scheduler& scheduler)
    : NBIO::Core::Channel<TcpReceiveChannel>(NBIO::Core::ChannelType::kReceive,
                                                   static_cast<std::uintptr_t>(connector.native_handle()), multiplexer,
                                                   scheduler),
      connector_(connector) {
    if (!connector_.IsValid()) {
        throw std::logic_error("invalid connector");
    }
    // A receive that blocks in the call would hold the whole engine until the peer
    // sent something, so the connection is non-blocking from here.
    if (auto result = connector_.NonBlocking(true); !result) {
        throw std::system_error(result.error(), "NonBlocking failed");
    }
    // Registered on the first arm(): nothing is watched until a receive queues.
}

TcpReceiveChannel::~TcpReceiveChannel() noexcept { multiplexer_.DeleteChannel(this); }

void TcpReceiveChannel::Prepare(Async::Coroutine waiter, Net::Transmission* transmission) {
    waiters_.push_back(std::move(waiter));
    auto& payload = payload_;
    payload.submit(transmission);
}

TcpReceiveChannel::Payload& TcpReceiveChannel::Submit() { return payload_; }

void TcpReceiveChannel::Complete() {
    auto& payload = payload_;
    while (auto completion = payload.next_completion()) {
        auto waiter = std::move(waiters_.front());
        waiters_.pop_front();
        scheduler_.Submit(std::move(waiter));
        payload.conclude();
    }
    if (payload.size() != 0) {
        Arm();
    } else {
        Disarm();
    }
}

NBIO::Async::Task<Utility::expected<std::size_t, std::error_code>> TcpReceiveChannel::Receive(
    std::span<char> buffer) {
    auto result = co_await ReceiveAwaiter{*this, buffer};
    if (result.status == OperationStatus::kError) {
        co_return Utility::unexpected<std::error_code>(std::move(result.error_code));
    }
    co_return result.bytes;
}
}  // namespace NBIO::Net




