#include <nbio/async.hpp>
#include <nbio/async/Task.hpp>
#include <nbio/net/Address.hpp>
#include <nbio/net/RdmaConnectService.hpp>
#include <nbio/net/RdmaResourceManager.hpp>

#include <cstring>
#include <iostream>
#include <string_view>
#include <utility>

nbio::async::Task<void> RunClient() {
    nbio::net::RdmaResourceManager resources{"siw0"};
    const auto address = nbio::net::Address::FromV4("192.168.0.101", 6666);
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

int main() { nbio::async::Run(RunClient()); }