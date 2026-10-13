#include <nbio/async/coroutine.hpp>
#include <nbio/runtime/daemon.hpp>
#include <nbio/async/scheduler.hpp>
#include <nbio/core/types.hpp>
#include <nbio/net/address.hpp>
#include <nbio/net/tcp_listen_channel.hpp>
#include <nbio/net/socket.hpp>
#include <optional>
#include <stdexcept>
#include <utility>

namespace nbio::net {
TcpListenChannel::TcpListenChannel(nbio::net::TcpListener& listener, nbio::core::Multiplexer& multiplexer,
                                   nbio::async::Scheduler& scheduler)
    : nbio::core::Channel<TcpListenChannel>(nbio::core::ChannelType::kAccept,
                                            static_cast<std::uintptr_t>(listener.native_handle()), multiplexer,
                                            scheduler),
      listener_(listener) {
    if (!listener_.is_valid()) {
        throw std::logic_error("invalid listener");
    }
    // The listener has to be polled rather than waited on: an accept that blocks in
    // the call would hold the whole engine.
    if (auto result = listener_.ToggleNonBlocking(true); !result) {
        throw std::system_error(result.error(), "NonBlocking failed");
    }
    // Registered on the first arm(): there is nothing to watch until a wait queues.
}

TcpListenChannel::~TcpListenChannel() noexcept { multiplexer_.DeleteChannel(this); }

class AcceptAwaiter {
   public:
    AcceptAwaiter(TcpListenChannel& channel) : channel_(channel) {}
    AcceptAwaiter(const AcceptAwaiter&) = delete;
    AcceptAwaiter& operator=(const AcceptAwaiter&) = delete;

    ~AcceptAwaiter() noexcept = default;

    bool await_ready() const noexcept { return false; }

    template <typename PromiseType>
    void await_suspend(std::coroutine_handle<PromiseType> handle) {
        channel_.Prepare(async::Coroutine::FromHandle(handle), &communication_);
        channel_.Arm();
    }

    net::Communication await_resume() noexcept { return std::move(communication_); }

   private:
    TcpListenChannel& channel_;
    net::Communication communication_{};
};

// The accept body, as a plain coroutine whose channel is an ordinary parameter
// (see the note on TcpListenChannel::accept).
static nbio::async::Task<utility::expected<std::pair<net::TcpConnector, net::Address>, std::error_code>> AcceptOn(
    TcpListenChannel& channel) {
    auto result = co_await AcceptAwaiter(channel);
    if (result.status == OperationStatus::kError) {
        co_return utility::unexpected<std::error_code>(std::move(result.error_code));
    }
    // The socket the kernel accepted is connected to a peer, so the listener is what
    // turns it into the connection this end now has; the address is the one thing
    // about it that the accepted side did not already know.
    co_return std::make_pair(channel.listener().Adopt(std::move(result.socket)), std::move(result.address));
}

void TcpListenChannel::Prepare(async::Coroutine waiter, net::Communication* communication) {
    communication->status = OperationStatus::kPending;
    communication->socket = {};
    communication->address = {};
    communication->address.length() = net::Address::capacity();
    communication->error_code = {};
    waiters_.emplace_back(std::move(waiter));
    auto& payload = payload_;
    payload.submit(communication);
}

TcpListenChannel::Payload& TcpListenChannel::Submit() { return payload_; }

void TcpListenChannel::Complete() {
    auto& payload = payload_;
    while (auto next = payload.next_completion()) {
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

nbio::async::Task<utility::expected<std::pair<net::TcpConnector, net::Address>, std::error_code>>
TcpListenChannel::Accept() {
    // Deliberately not a member coroutine: the implicit object parameter of a
    // member coroutine is laid out by the compiler in the same frame slot the
    // promise uses, so `*this` inside the body came back as the inherited
    // control block instead of the channel. Taking the channel as an ordinary
    // parameter keeps it in the parameter area.
    return AcceptOn(*this);
}
}  // namespace nbio::net
