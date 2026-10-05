#include <NBIO/Net/TcpConnectChannel.hpp>

#include <NBIO/Async/Coroutine.hpp>
#include <NBIO/Net/Payload.hpp>
#include <NBIO/Async/Runtime.hpp>
#include <optional>
#include <stdexcept>
#include <system_error>
#include <utility>

namespace NBIO::Net {
TcpConnectChannel::TcpConnectChannel(NBIO::Net::TcpConnector connector,
                                     NBIO::Core::Multiplexer& multiplexer,
                                     NBIO::Async::Scheduler& scheduler)
    :  // The parameter is built before the base is, so the socket it holds is what the
       // channel says it watches; the member takes it over from there.
      NBIO::Core::Channel<TcpConnectChannel>(NBIO::Core::ChannelType::kConnect,
                                                   static_cast<std::uintptr_t>(connector.native_handle()), multiplexer,
                                                   scheduler),
      connector_(std::move(connector)) {
    if (!connector_.IsValid()) {
        throw std::logic_error("invalid connector");
    }
    // A connect that blocks in the call would hold the whole engine for as long as the
    // handshake takes, so the socket is non-blocking from here: what it reports instead
    // is a connect under way, which is what this channel then waits for.
    if (auto result = connector_.NonBlocking(true); !result) {
        throw std::system_error(result.error(), "NonBlocking failed");
    }
    // Registered on the first arm(): nothing is watched until a connect queues.
}

TcpConnectChannel::~TcpConnectChannel() noexcept { multiplexer_.DeleteChannel(this); }

class ConnectAwaiter {
   public:
    ConnectAwaiter(TcpConnectChannel& channel, const NBIO::Net::Address* source,
                   const NBIO::Net::Address& target)
        : channel_(channel), source_(source), target_(target) {}

    ConnectAwaiter(const ConnectAwaiter&) = delete;
    ConnectAwaiter& operator=(const ConnectAwaiter&) = delete;

    // No cancellation hook. The Parked Coroutine keeps this frame's control block
    // alive and the channel owns the waiter until it answers it, so a stale
    // registration is impossible rather than detected.

    bool await_ready() const noexcept { return false; }

    template <typename PromiseType>
    bool await_suspend(std::coroutine_handle<PromiseType> handle) {
        if (source_ != nullptr) {
            if (auto bound = channel_.connector_.Bind(*source_); !bound) {
                failure_ = bound.error();
                return false;
            }
        }
        if (auto started = channel_.connector_.StartConnect(target_); !started) {
            // Refused before the kernel took it -- an address that does not resolve, a
            // socket that is already connected -- so there is nothing to wait for.
            failure_ = started.error();
            return false;
        }
        channel_.Park(Async::Coroutine::FromHandle(handle));
        channel_.Arm();
        return true;
    }

    Utility::expected<NBIO::Net::TcpConnector, std::error_code> await_resume() noexcept {
        if (failure_.has_value()) {
            return Utility::unexpected<std::error_code>(*failure_);
        }
        // The wait is over, so the socket has the answer: made, or the reason it was
        // not. Either way it is the end of this channel's life, so the connection goes
        // back out with it.
        if (auto settled = channel_.connector_.FinishConnect(); !settled) {
            return Utility::unexpected<std::error_code>(settled.error());
        }
        return std::move(channel_.connector_);
    }

   private:
    TcpConnectChannel& channel_;
    // Pointed at rather than held: the source address belongs to the caller, and the
    // awaiter outlives the call that named it.
    const NBIO::Net::Address* source_{nullptr};
    NBIO::Net::Address target_{};
    std::optional<std::error_code> failure_{};
};

// The connect bodies, as plain coroutines whose channel is an ordinary parameter:
// the implicit object parameter of a member coroutine is laid out where the promise
// lives, so `*this` inside the body comes back as the inherited control block instead
// of the channel. Keeping it a parameter keeps it in the parameter area.
static NBIO::Async::Task<Utility::expected<NBIO::Net::TcpConnector, std::error_code>> ConnectOn(
    TcpConnectChannel& channel, const NBIO::Net::Address* source,
    const NBIO::Net::Address& target) {
    co_return co_await ConnectAwaiter{channel, source, target};
}

void TcpConnectChannel::Complete() {
    auto& payload = payload_;
    payload.ReleasePoll();

    // The socket stays writable once it is, so a registration left in place would
    // report readiness on every pass from here on. A connection is made once: this is
    // the end of the wait, not a pause in it.
    Disarm();

    if (!waiter_) [[unlikely]] {
        return;
    }
    auto waiter = std::exchange(waiter_, {});
    scheduler_.Submit(std::move(waiter));
}

TcpConnectChannel::Payload& TcpConnectChannel::Submit() { return payload_; }

NBIO::Async::Task<Utility::expected<NBIO::Net::TcpConnector, std::error_code>> TcpConnectChannel::Connect(
    const NBIO::Net::Address& target) {
    return ConnectOn(*this, nullptr, target);
}

NBIO::Async::Task<Utility::expected<NBIO::Net::TcpConnector, std::error_code>> TcpConnectChannel::Connect(
    const NBIO::Net::Address& source, const NBIO::Net::Address& target) {
    return ConnectOn(*this, &source, target);
}
}  // namespace NBIO::Net




