#include <nbio/async/Async.hpp>
#include <nbio/async/Task.hpp>
#include <nbio/net/Address.hpp>
#include <nbio/net/RdmaAcceptService.hpp>
#include <nbio/net/RdmaResourceManager.hpp>

#include <iostream>
#include <string_view>
#include <utility>

nbio::async::Task<nbio::Runtime, void> RunServer() {
    nbio::net::RdmaResourceManager resources{"siw0"};
    const auto address = nbio::net::Address::FromV4("192.168.0.101", 6666);
    nbio::net::RdmaAcceptService acceptor{resources, address};
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

int main() { nbio::Run(RunServer()); }