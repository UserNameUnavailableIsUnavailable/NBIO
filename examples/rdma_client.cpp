#include <CLI/CLI.hpp>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <nbio/async.hpp>
#include <nbio/net.hpp>
#include <span>
#include <string_view>
#include <utility>

namespace {
// The caller owns every byte that is registered: one block for sending and one for
// receiving, both page aligned because that is the granularity a registration pins.
struct Arena {
    std::size_t bytes{0};
    char* send{nullptr};
    char* receive{nullptr};

    explicit Arena(std::size_t size) : bytes(size) {
        const auto page = nbio::net::RdmaResource::page_size();
        send = static_cast<char*>(std::aligned_alloc(page, bytes));
        receive = static_cast<char*>(std::aligned_alloc(page, bytes));
    }

    ~Arena() {
        std::free(send);
        std::free(receive);
    }

    Arena(const Arena&) = delete;
    Arena& operator=(const Arena&) = delete;

    std::span<char> send_space() const { return {send, bytes}; }
    std::span<char> receive_space() const { return {receive, bytes}; }
};
}  // namespace

nbio::async::Task<void> RunClient(std::string_view device, std::string_view ip, std::uint16_t port) {
    Arena arena{1 << 20};
    nbio::net::RdmaResource resource{device, arena.send_space(), arena.receive_space()};
    const auto address = nbio::net::Address::FromIPv4(ip, port);
    if (!address) {
        std::cerr << "Invalid RDMA address: " << address.error().message() << '\n';
        co_return;
    }
    nbio::net::RdmaConnectService connector{resource, *address};
    auto connected = co_await connector.Connect();
    if (!connected) {
        std::cerr << "Failed to create an RDMA connection: " << connected.error().message() << '\n';
        co_return;
    }
    auto session = std::make_shared<nbio::net::RdmaSessionService>(std::move(*connected));
    if (auto up = co_await session->Establish(); !up) {
        std::cerr << "Failed to establish the RDMA connection: " << up.error().message() << '\n';
        co_return;
    }

    constexpr std::string_view message{"Hello, RDMA!"};
    // A subspan of the send region, filled in place: nothing is copied into a pool.
    auto buffer = arena.send_space().first(message.size());
    std::memcpy(buffer.data(), message.data(), message.size());

    auto posted = session->SubmitSend(buffer);
    if (!posted) {
        std::cerr << "Failed to post RDMA data: " << posted.error().message() << '\n';
        co_return;
    }
    if (auto retired = co_await session->WaitSend(*posted); !retired) {
        std::cerr << "Failed to complete RDMA send: " << retired.error().message() << '\n';
        co_return;
    }
    auto peeked = session->PeekSend(*posted);
    if (!peeked) {
        std::cerr << "Failed to peek RDMA send: " << peeked.error().message() << '\n';
        co_return;
    }
    session->CompleteSend(*posted);
    std::cout << "Sent " << peeked->buffer.size() << " bytes\n";
}

int main(int argc, char* argv[]) {
    CLI::App app{"RDMA Client"};
    std::string ip;
    std::string device;
    std::uint16_t port{6666};
    app.add_option("--ip", ip, "RDMA server IP address");
    app.add_option("--port", port, "RDMA server port");
    app.add_option("--device", device, "RDMA device name");
    CLI11_PARSE(app, argc, argv);
    nbio::async::Run(RunClient(device, ip, port));
}
