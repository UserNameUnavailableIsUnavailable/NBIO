#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <CLI/CLI.hpp>
#include <nbio/async.hpp>
#include <nbio/net.hpp>
#include <nbio/fs.hpp>

using namespace nbio;
namespace stdfs = std::filesystem;

async::Task<void> sender(std::string file_path) {
    fs::FileView file_view(file_path);
    std::span<char> send_buffer(static_cast<char*>(file_view.data()), file_view.size());
    auto raw = new (std::align_val_t(4096)) char[4096];
    std::unique_ptr<char[]> raw_ptr(raw);
    auto receive_buffer = std::span<char>(raw_ptr.get(), 4096);
    net::RdmaResource rdma_resource("siw0", send_buffer, receive_buffer);
    auto address = net::Address::FromIPv4("192.168.4.146", 12345);
    net::RdmaListenService listener(rdma_resource);
    if (auto bound = listener.Bind(*address); !bound) {
        std::cerr << "failed to bind: " << bound.error().message() << std::endl;
        co_return;
    }
    if (auto listening = listener.Listen(); !listening) {
        std::cerr << "failed to listen: " << listening.error().message() << std::endl;
        co_return;
    }
    auto accepted = co_await listener.Accept();
    if (!accepted) {
        std::cerr << "failed to accept: " << accepted.error().message() << std::endl;
        co_return;
    }
    auto session = std::make_shared<net::RdmaSessionService>(std::move(*accepted));
    if (auto up = co_await session->Establish(); !up) {
        std::cerr << "failed to establish: " << up.error().message() << std::endl;
        co_return;
    }
    auto seq = session->SubmitSend(send_buffer);
    if (!seq) {
        std::cerr << "failed to submit: " << seq.error().message() << std::endl;
        co_return;
    }
    auto waited = co_await session->WaitSend(*seq);
    if (!waited) {
        std::cerr << "failed to wait: " << waited.error().message() << std::endl;
        co_return;
    }
    auto peeked = session->PeekSend(*seq);
    if (!peeked) {
        std::cerr << "failed to peek: " << peeked.error().message() << std::endl;
        co_return;
    }
    session->CompleteSend(*seq);
    std::cout << "successfully sent " << peeked->buffer.size() << " bytes." << std::endl;
}

async::Task<void> receiver(std::string file_path) {
    auto raw_receive_buffer = new (std::align_val_t(4096)) char[4096];
    auto raw_send_buffer = new (std::align_val_t(4096)) char[4096];
    std::unique_ptr<char[]> raw_receive_ptr(raw_receive_buffer);
    std::unique_ptr<char[]> raw_send_ptr(raw_send_buffer);
    auto receive_buffer = std::span<char>(raw_receive_ptr.get(), 4096);
    auto send_buffer = std::span<char>(raw_send_ptr.get(), 4096);
    net::RdmaResource rdma_resource("siw0", send_buffer, receive_buffer);
    auto address = net::Address::FromIPv4("192.168.4.146", 12345);
    net::RdmaConnectService connector(rdma_resource, *address);
    auto connected = co_await connector.Connect();
    if (!connected) {
        std::cerr << "failed to connect: " << connected.error().message() << std::endl;
        co_return;
    }
    auto session = std::make_shared<net::RdmaSessionService>(std::move(*connected));
    // The session is prepared, so the receive can be posted before the connection is
    // established: that is what closes the window where the peer may already send.
    auto seq = session->SubmitReceive(receive_buffer);
    if (!seq) {
        std::cerr << "failed to submit: " << seq.error().message() << std::endl;
        co_return;
    }
    if (auto up = co_await session->Establish(); !up) {
        std::cerr << "failed to establish: " << up.error().message() << std::endl;
        co_return;
    }
    auto waited = co_await session->WaitReceive(*seq);
    if (!waited) {
        std::cerr << "failed to wait: " << waited.error().message() << std::endl;
        co_return;
    }
    auto peeked = session->PeekReceive(*seq);
    if (!peeked) {
        std::cerr << "failed to peek: " << peeked.error().message() << std::endl;
        co_return;
    }
    std::cout << "successfully received " << peeked->bytes << " bytes." << std::endl;
    stdfs::path output_path(file_path);
    std::ofstream output(output_path, std::ios::binary);
    output.write(peeked->buffer.data(), static_cast<std::streamsize>(peeked->bytes));
    session->CompleteReceive(*seq);
    std::cout << "saved to " << output_path << "." << std::endl;
}

int main(int argc, char* argv[]) {
    CLI::App app{"RDMA File Sender"};
    std::string file_path;
    std::string mode; // "sender" | "receiver"
    app.add_option("-m,--mode", mode, "Mode of operation")
    ->check(CLI::IsMember({"sender", "receiver"}))
    ->required();
    app.add_option("-f,--file", file_path, "File to send")->required();
    CLI11_PARSE(app, argc, argv);
    if (mode == "sender") {
        async::Run(sender(file_path));
    } else {
        async::Run(receiver(file_path));
    }
}