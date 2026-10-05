#include <NBIO/Net/TcpAcceptChannel.hpp>

#include <NBIO/Async/Coroutine.hpp>
#include <NBIO/Async/Scheduler.hpp>
#include <NBIO/Net/Address.hpp>
#include <NBIO/Net/TcpSocket.hpp>
#include <NBIO/Core/Types.hpp>
#include <NBIO/Async/Runtime.hpp>
#include <optional>
#include <stdexcept>
#include <utility>

namespace NBIO::Net {
TcpAcceptChannel::TcpAcceptChannel(NBIO::Net::TcpAcceptor& acceptor, NBIO::Core::Multiplexer& multiplexer,
                                   NBIO::Async::Scheduler& scheduler)
    : NBIO::Core::Channel<TcpAcceptChannel>(NBIO::Core::ChannelType::kAccept,
                                                  static_cast<std::uintptr_t>(acceptor.native_handle()), multiplexer,
                                                  scheduler),
      acceptor_(acceptor) {
    if (!acceptor_.is_valid()) {
        throw std::logic_error("invalid acceptor");
    }
    // The listener has to be polled rather than waited on: an accept that blocks in
    // the call would hold the whole engine.
    if (auto result = acceptor_.NonBlocking(true); !result) {
        throw std::system_error(result.error(), "NonBlocking failed");
    }
    // Registered on the first arm(): there is nothing to watch until a wait queues.
}

TcpAcceptChannel::~TcpAcceptChannel() noexcept { multiplexer_.DeleteChannel(this); }

class AcceptAwaiter {
   public:
    AcceptAwaiter(TcpAcceptChannel& channel) : channel_(channel) {}
    AcceptAwaiter(const AcceptAwaiter&) = delete;
    AcceptAwaiter& operator=(const AcceptAwaiter&) = delete;

    ~AcceptAwaiter() noexcept = default;

    bool await_ready() const noexcept { return false; }

    template <typename PromiseType>
    void await_suspend(std::coroutine_handle<PromiseType> handle) {
        channel_.Prepare(Async::Coroutine::FromHandle(handle), &communication_);
        channel_.Arm();
    }

    Net::Communication await_resume() noexcept { return std::move(communication_); }

   private:
    TcpAcceptChannel& channel_;
    Net::Communication communication_{};
};

// The accept body, as a plain coroutine whose channel is an ordinary parameter
// (see the note on TcpAcceptChannel::accept).
static NBIO::Async::Task<Utility::expected<std::pair<Net::TcpConnector, Net::Address>, std::error_code>>
AcceptOn(TcpAcceptChannel& channel) {
    auto result = co_await AcceptAwaiter(channel);
    if (result.status == OperationStatus::kError) {
        co_return Utility::unexpected<std::error_code>(std::move(result.error_code));
    }
    // The socket the kernel accepted is connected to a peer, so the listener is what
    // turns it into the connection this end now has; the address is the one thing
    // about it that the accepted side did not already know.
    co_return std::make_pair(channel.acceptor().Adopt(std::move(result.socket)), std::move(result.address));
}

void TcpAcceptChannel::Prepare(Async::Coroutine waiter, Net::Communication* communication) {
    communication->status = OperationStatus::kPending;
    communication->socket = {};
    communication->address = {};
    communication->address.length() = Net::Address::capacity();
    communication->error_code = {};
    waiters_.emplace_back(std::move(waiter));
    auto& payload = payload_;
    payload.submit(communication);
}

TcpAcceptChannel::Payload& TcpAcceptChannel::Submit() { return payload_; }

void TcpAcceptChannel::Complete() {
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

NBIO::Async::Task<Utility::expected<std::pair<Net::TcpConnector, Net::Address>, std::error_code>>
TcpAcceptChannel::Accept() {
    // Deliberately not a member coroutine: the implicit object parameter of a
    // member coroutine is laid out by the compiler in the same frame slot the
    // promise uses, so `*this` inside the body came back as the inherited
    // control block instead of the channel. Taking the channel as an ordinary
    // parameter keeps it in the parameter area.
    return AcceptOn(*this);
}
}  // namespace NBIO::Net




