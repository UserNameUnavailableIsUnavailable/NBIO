#include <NBIO/Async.hpp>
#include <NBIO/Async/Task.hpp>
#include <NBIO/Net/Address.hpp>
#include <NBIO/Net/RdmaAcceptService.hpp>
#include <NBIO/Net/RdmaResourceManager.hpp>

#include <iostream>
#include <string_view>
#include <utility>

NBIO::Async::Task<void> RunServer() {
    NBIO::Net::RdmaResourceManager resources{"siw0"};
    const auto address = NBIO::Net::Address::FromV4("192.168.0.101", 6666);
    NBIO::Net::RdmaAcceptService acceptor{resources, address};
    std::cout << "Listening on RDMA " << address.ip() << ':' << address.port() << '\n';

    auto accepted = co_await acceptor.Accept();
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
    if (received->state != NBIO::Net::RdmaReceiveState::kData) {
        std::cerr << "RDMA peer closed before sending data\n";
        co_return;
    }
    const std::string_view message{received->data.data(), received->data.size()};
    std::cout << "Received: " << message << '\n';
    if (auto released = session->Release(received->data); !released) {
        std::cerr << "Failed to release RDMA receive buffer: " << released.error().message() << '\n';
    }
}

int main() { NBIO::Async::Run(RunServer()); }