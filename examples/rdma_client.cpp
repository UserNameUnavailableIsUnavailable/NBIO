#include <CLI/CLI.hpp>
#include <nbio/async.hpp>
#include <nbio/net.hpp>

#include <cstring>
#include <iostream>
#include <string_view>
#include <utility>

nbio::async::Task<void> RunClient(std::string_view device, std::string_view ip, std::uint16_t port) {
    nbio::net::RdmaResourceManager resources{device};
    const auto address = nbio::net::Address::FromV4(ip, port);
    nbio::net::RdmaConnectService connector{resources};
    auto connected = co_await connector.Connect(address);
    if (!connected) {
        std::cerr << "Failed to connect to RDMA address: " << connected.error().message() << '\n';
        co_return;
    }
    auto session = std::move(*connected);
    auto acquired = session->send_channel().Acquire();
    if (!acquired) {
        std::cerr << "Failed to acquire RDMA send buffer: " << acquired.error().message() << '\n';
        co_return;
    }
    if (acquired->state != nbio::net::RdmaBufferState::kAvailable) {
        std::cerr << "No RDMA send buffer is available\n";
        co_return;
    }
    constexpr std::string_view message{"Hello, RDMA!"};
    std::memcpy(acquired->buffer.data(), message.data(), message.size());
    if (auto sent = session->Send(acquired->buffer, message.size()); !sent) {
        std::cerr << "Failed to send RDMA data: " << sent.error().message() << '\n';
        co_return;
    }
    if (auto completed = co_await session->PollSend(); !completed) {
        std::cerr << "Failed to complete RDMA send: " << completed.error().message() << '\n';
        co_return;
    }
    std::cout << "Sent: " << message << '\n';
}

int main(int argc, char* argv[])
{
    CLI::App app{"RDMA Client"};
    std::string device;
    std::string ip;
    std::uint16_t port;
    app.add_option("--device", device, "RDMA device name");
    app.add_option("--ip", ip, "RDMA server IP address");
    app.add_option("--port", port, "RDMA server port");
    CLI11_PARSE(app, argc, argv);
    nbio::async::Run(RunClient(device, ip, port));
}