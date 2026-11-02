#include <CLI/CLI.hpp>
#include <cstdlib>
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

nbio::async::Task<void> RunServer(std::string_view device, std::string_view ip, std::uint16_t port) {
    Arena arena{1 << 20};
    nbio::net::RdmaResource resource{device, arena.send_space(), arena.receive_space()};
    const auto address = nbio::net::Address::FromIPv4(ip, port);
    if (!address) {
        std::cerr << "Invalid RDMA address: " << address.error().message() << '\n';
        co_return;
    }
    nbio::net::RdmaListenService listener{resource};
    if (auto bound = listener.Bind(*address); !bound) {
        std::cerr << "Failed to bind the RDMA address: " << bound.error().message() << '\n';
        co_return;
    }
    if (auto listening = listener.Listen(); !listening) {
        std::cerr << "Failed to listen on the RDMA address: " << listening.error().message() << '\n';
        co_return;
    }
    std::cout << "Listening on RDMA " << *(address->ip()) << ':' << *(address->port()) << '\n';

    auto accepted = co_await listener.Accept();
    if (!accepted) {
        std::cerr << "Failed to accept RDMA connection: " << accepted.error().message() << '\n';
        co_return;
    }
    auto session = std::make_shared<nbio::net::RdmaSessionService>(std::move(*accepted));

    // A receive has to be posted before the peer sends, and the connection is only
    // prepared at this point: posting now is what closes the window.
    auto posted = session->SubmitReceive(arena.receive_space());
    if (!posted) {
        std::cerr << "Failed to post an RDMA receive: " << posted.error().message() << '\n';
        co_return;
    }
    if (auto up = co_await session->Establish(); !up) {
        std::cerr << "Failed to establish the RDMA connection: " << up.error().message() << '\n';
        co_return;
    }
    if (auto received = co_await session->WaitReceive(*posted); !received) {
        std::cerr << "Failed to receive RDMA data: " << received.error().message() << '\n';
        co_return;
    }
    auto peeked = session->PeekReceive(*posted);
    if (!peeked) {
        std::cerr << "Failed to peek RDMA receive: " << peeked.error().message() << '\n';
        co_return;
    }
    const std::string_view message{peeked->buffer.data(), peeked->bytes};
    std::cout << "Received: " << message << '\n';
    session->CompleteReceive(*posted);
}

int main(int argc, char* argv[]) {
    CLI::App app{"RDMA Server"};
    std::string ip;
    std::string device;
    std::uint16_t port{6666};
    app.add_option("--ip", ip, "RDMA server IP address");
    app.add_option("--port", port, "RDMA server port");
    app.add_option("--device", device, "RDMA device name");
    CLI11_PARSE(app, argc, argv);
    nbio::async::Run(RunServer(device, ip, port));
}
