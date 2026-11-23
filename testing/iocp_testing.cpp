#include <gtest/gtest.h>

#include <nbio/core/iocp_multiplexer.hpp>
#include <nbio/net/tcp_receive_channel.hpp>
#include <nbio/net/tcp_send_channel.hpp>
#include <nbio/time/timer_channel.hpp>
#include <thread>

using namespace std::chrono_literals;

namespace {
nbio::async::Task<void> Sleep(nbio::time::TimerChannel& channel, std::chrono::milliseconds delay, bool& done) {
    co_await channel.Sleep(std::chrono::steady_clock::now() + delay);
    done = true;
}

void RunUntil(nbio::async::Scheduler& scheduler, const bool& done) {
    const auto deadline = std::chrono::steady_clock::now() + 3s;
    while (!done && std::chrono::steady_clock::now() < deadline) scheduler.Run();
    ASSERT_TRUE(done);
}

nbio::async::Task<void> Send(nbio::net::TcpSendChannel& channel, std::span<const char> buffer, bool& done,
                             std::size_t& bytes) {
    auto result = co_await channel.Send(buffer);
    EXPECT_TRUE(result);
    if (result) bytes = *result;
    done = true;
}

nbio::async::Task<void> Receive(nbio::net::TcpReceiveChannel& channel, std::span<char> buffer, bool& done,
                                std::size_t& bytes) {
    auto result = co_await channel.Receive(buffer);
    EXPECT_TRUE(result);
    if (result) bytes = *result;
    done = true;
}

nbio::async::Task<void> SendFailure(nbio::net::TcpSendChannel& channel, bool& done, std::error_code& error) {
    const char byte = 'x';
    auto result = co_await channel.Send(std::span(&byte, 1));
    EXPECT_FALSE(result);
    if (!result) error = result.error();
    done = true;
}
}  // namespace

TEST(WindowsTimer, DurationCancellationAndRearming) {
    nbio::time::Timer timer;
    timer.NonBlocking();
    timer.fire_after(30ms);
    auto pending = timer.wait();
    ASSERT_FALSE(pending);
    EXPECT_EQ(pending.error(), std::errc::operation_would_block);
    timer.cancel();
    std::this_thread::sleep_for(40ms);
    EXPECT_FALSE(timer.wait());

    timer.NonBlocking(false);
    const auto start = std::chrono::steady_clock::now();
    timer.fire_after(20ms);
    ASSERT_TRUE(timer.wait());
    EXPECT_GE(std::chrono::steady_clock::now() - start, 15ms);
    timer.fire_after(-1ns);
    EXPECT_TRUE(timer.wait());
}

TEST(IOCP, TimerDispatchAndRearm) {
    nbio::core::IOCPMultiplexer multiplexer;
    nbio::async::Scheduler scheduler([&](bool) { multiplexer.WaitFor(10ms); });
    nbio::time::Timer timer;
    nbio::time::TimerChannel channel(timer, multiplexer, scheduler);
    bool first = false;
    bool second = false;
    scheduler.Spawn(Sleep(channel, 20ms, first));
    scheduler.Spawn(Sleep(channel, 50ms, second));
    RunUntil(scheduler, first);
    RunUntil(scheduler, second);
    EXPECT_FALSE(channel.is_armed());
}

TEST(IOCP, UnregisterPendingAndQueuedTimerBeforeDestruction) {
    nbio::core::IOCPMultiplexer multiplexer;
    nbio::async::Scheduler scheduler([](bool) {});
    nbio::time::Timer timer;
    for (const auto delay : {1s, 0s}) {
        {
            nbio::time::TimerChannel channel(timer, multiplexer, scheduler);
            timer.fire_after(delay);
            channel.Arm();
            multiplexer.WaitFor(0ms);
            if (delay == 0s) std::this_thread::sleep_for(20ms);
        }
        multiplexer.WaitFor(0ms);
    }
}

TEST(IOCP, TimeoutAndUnsupportedChannelLeaveArmingConsistent) {
    nbio::core::IOCPMultiplexer multiplexer;
    const auto start = std::chrono::steady_clock::now();
    multiplexer.WaitFor(20ms);
    EXPECT_GE(std::chrono::steady_clock::now() - start, 15ms);
    multiplexer.WaitFor(-1ms);
    nbio::async::Scheduler scheduler([](bool) {});
    nbio::core::ChannelBase unsupported(nbio::core::ChannelType::kNotify, 0, multiplexer, scheduler);
    EXPECT_THROW(unsupported.Arm(), std::system_error);
    EXPECT_FALSE(unsupported.is_armed());
    nbio::time::Timer timer;
    nbio::time::TimerChannel channel(timer, multiplexer, scheduler);
    timer.fire_after(20ms);
    channel.Arm();
    const auto before = std::chrono::steady_clock::now();
    multiplexer.Wait();
    EXPECT_GE(std::chrono::steady_clock::now() - before, 15ms);
    EXPECT_FALSE(channel.is_armed());
}

TEST(IOCP, VectorSocketSendReceiveAndEmptyBuffer) {
    nbio::net::Socket listener(nbio::net::Address::Family::kIPv4, nbio::net::Socket::Type::kStream);
    auto address = nbio::net::Address::FromIPv4("127.0.0.1", 0);
    ASSERT_TRUE(address);
    ASSERT_TRUE(listener.Bind(*address));
    ASSERT_TRUE(listener.Listen());
    auto local = listener.GetLocalAddress();
    ASSERT_TRUE(local);
    nbio::net::TcpConnector client;
    ASSERT_TRUE(client.Connect(*local));
    auto peer = listener.Accept();
    ASSERT_TRUE(peer);

    nbio::core::IOCPMultiplexer multiplexer;
    nbio::async::Scheduler scheduler([&](bool) { multiplexer.WaitFor(10ms); });
    nbio::net::TcpSendChannel send(client, multiplexer, scheduler);
    nbio::net::TcpReceiveChannel receive(client, multiplexer, scheduler);
    const std::string first = "hello";
    const std::string second = " world";
    bool a = false, b = false, empty = false;
    std::size_t na = 0, nb = 0, ne = 1;
    scheduler.Spawn(Send(send, {}, empty, ne));
    scheduler.Spawn(Send(send, first, a, na));
    scheduler.Spawn(Send(send, second, b, nb));
    RunUntil(scheduler, b);
    EXPECT_TRUE(a);
    EXPECT_TRUE(empty);
    EXPECT_EQ(ne, 0);
    EXPECT_EQ(na, first.size());
    EXPECT_EQ(nb, second.size());
    char data[11]{};
    std::size_t received = 0;
    while (received < sizeof(data)) {
        auto result = peer->first.Receive(std::span(data).subspan(received));
        ASSERT_TRUE(result);
        ASSERT_GT(*result, 0);
        received += *result;
    }
    EXPECT_EQ(std::string(data, sizeof(data)), first + second);

    ASSERT_TRUE(peer->first.Send(std::span<const char>(data)));
    char part1[5]{}, part2[6]{};
    a = b = false;
    scheduler.Spawn(Receive(receive, part1, a, na));
    scheduler.Spawn(Receive(receive, part2, b, nb));
    RunUntil(scheduler, b);
    EXPECT_TRUE(a);
    EXPECT_EQ(std::string(part1, na) + std::string(part2, nb), first + second);

    peer->first.Shutdown(nbio::net::Socket::ShutdownHow::kWrite);
    a = b = false;
    scheduler.Spawn(Receive(receive, part1, a, na));
    scheduler.Spawn(Receive(receive, part2, b, nb));
    RunUntil(scheduler, b);
    EXPECT_TRUE(a);
    EXPECT_EQ(na, 0);
    EXPECT_EQ(nb, 0);
}

TEST(IOCP, ImmediateSocketErrorCompletesTheBatch) {
    nbio::net::TcpConnector client;
    nbio::core::IOCPMultiplexer multiplexer;
    nbio::async::Scheduler scheduler([&](bool) { multiplexer.WaitFor(10ms); });
    nbio::net::TcpSendChannel channel(client, multiplexer, scheduler);
    bool first = false, second = false;
    std::error_code a, b;
    scheduler.Spawn(SendFailure(channel, first, a));
    scheduler.Spawn(SendFailure(channel, second, b));
    RunUntil(scheduler, second);
    EXPECT_TRUE(first);
    EXPECT_EQ(a.value(), WSAENOTCONN);
    EXPECT_EQ(a, b);
    EXPECT_FALSE(channel.is_armed());
}

TEST(IOCP, PendingSocketReadCanBeDeletedAndRearmed) {
    nbio::net::Socket listener(nbio::net::Address::Family::kIPv4, nbio::net::Socket::Type::kStream);
    auto address = nbio::net::Address::FromIPv4("127.0.0.1", 0);
    ASSERT_TRUE(address);
    ASSERT_TRUE(listener.Bind(*address));
    ASSERT_TRUE(listener.Listen());
    auto local = listener.GetLocalAddress();
    ASSERT_TRUE(local);
    nbio::net::TcpConnector client;
    ASSERT_TRUE(client.Connect(*local));
    auto peer = listener.Accept();
    ASSERT_TRUE(peer);
    nbio::core::IOCPMultiplexer multiplexer;
    nbio::async::Scheduler scheduler([&](bool) { multiplexer.WaitFor(0ms); });
    nbio::net::TcpReceiveChannel receive(client, multiplexer, scheduler);
    char data[1]{};
    bool done = false;
    std::size_t bytes = 0;
    scheduler.Spawn(Receive(receive, data, done, bytes));
    scheduler.Run();
    multiplexer.WaitFor(0ms);
    receive.Disarm();
    receive.Arm();
    ASSERT_TRUE(peer->first.Send(std::span<const char>("x", 1)));
    RunUntil(scheduler, done);
    EXPECT_EQ(bytes, 1);
    EXPECT_EQ(data[0], 'x');
}
