#include <CLI/CLI.hpp>
#include <nbio/async.hpp>
#include <nbio/net.hpp>

#include <iostream>
#include <string_view>
#include <utility>

nbio::async::Task<void> RunServer(std::string_view device, std::string_view ip, std::uint16_t port) {
    nbio::net::RdmaResourceManager resources{device};
    const auto address = nbio::net::Address::FromV4(ip, port);
    std::unique_ptr<nbio::net::RdmaAcceptService> acceptor;
    try {
        acceptor = std::make_unique<nbio::net::RdmaAcceptService>(resources, address);
    } catch (const std::exception& e) {
        std::cerr << "Failed to start RDMA server: " << e.what() << '\n';
        co_return;
    }
    std::cout << "Listening on RDMA " << address.ip() << ':' << address.port() << '\n';

    auto accepted = co_await acceptor->Accept();
    if (!accepted) {
        std::cerr << "Failed to accept RDMA connection: " << accepted.error().message() << '\n';
        co_return;
    }
    auto session = std::move(*accepted);
    auto received = co_await session->Receive();
    if (!received) {
        std::cerr << "Failed to receive RDMA data: " << received.error().message() << '\n';
        co_return;
    }
    if (received->state != nbio::net::RdmaReceiveState::kData) {
        std::cerr << "RDMA peer closed before sending data\n";
        co_return;
    }
    const std::string_view message{received->data.data(), received->data.size()};
    std::cout << "Received: " << message << '\n';
    if (auto released = session->Release(received->data); !released) {
        std::cerr << "Failed to release RDMA receive buffer: " << released.error().message() << '\n';
    }
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