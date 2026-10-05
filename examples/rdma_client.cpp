#include <NBIO/Async.hpp>
#include <NBIO/Async/Task.hpp>
#include <NBIO/Net/Address.hpp>
#include <NBIO/Net/RdmaConnectService.hpp>
#include <NBIO/Net/RdmaResourceManager.hpp>

#include <cstring>
#include <iostream>
#include <string_view>
#include <utility>

NBIO::Async::Task<void> RunClient() {
    NBIO::Net::RdmaResourceManager resources{"siw0"};
    const auto address = NBIO::Net::Address::FromV4("192.168.0.101", 6666);
    NBIO::Net::RdmaConnectService connector{resources};
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
    if (acquired->state != NBIO::Net::RdmaBufferState::kAvailable) {
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

int main() { NBIO::Async::Run(RunClient()); }