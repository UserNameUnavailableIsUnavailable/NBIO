#if defined(_WIN32)
#include <Windows.h>
#include <winsock2.h>

#include <algorithm>
#include <atomic>
#include <limits>
#include <nbio/core/iocp_multiplexer.hpp>
#include <nbio/fs/file_read_channel.hpp>
#include <nbio/fs/file_stream.hpp>
#include <nbio/fs/file_write_channel.hpp>
#include <nbio/net/tcp_receive_channel.hpp>
#include <nbio/net/tcp_send_channel.hpp>
#include <nbio/time/timer_channel.hpp>
#include <unordered_map>
#include <variant>

namespace nbio::core {
namespace {
using DataChannel =
    std::variant<fs::FileReadChannel*, fs::FileWriteChannel*, net::TcpReceiveChannel*, net::TcpSendChannel*>;

DataChannel AsDataChannel(ChannelBase* channel) {
    switch (channel->type()) {
        case ChannelType::kRead:
            return static_cast<fs::FileReadChannel*>(channel);
        case ChannelType::kWrite:
            return static_cast<fs::FileWriteChannel*>(channel);
        case ChannelType::kReceive:
            return static_cast<net::TcpReceiveChannel*>(channel);
        case ChannelType::kSend:
            return static_cast<net::TcpSendChannel*>(channel);
        default:
            throw std::system_error(std::make_error_code(std::errc::operation_not_supported), "IOCP channel type");
    }
}

struct Operation : OVERLAPPED {
    Operation(ChannelBase* channel, HANDLE port) : OVERLAPPED{}, channel(channel), port(port) {}
    ~Operation() {
        if (hEvent) ::CloseHandle(hEvent);
    }

    ChannelBase* channel;
    HANDLE port;
    HANDLE wait{};
    std::atomic_bool posted{false};
    bool synthetic{false};
    bool empty{false};
    DWORD error{};
    DWORD flags{};
    std::vector<WSABUF> buffers;
};

void Post(Operation& operation, DWORD error = ERROR_SUCCESS) {
    operation.error = error;
    operation.synthetic = true;
    if (!::PostQueuedCompletionStatus(operation.port, 0, 0, &operation)) {
        // A submitted operation must never disappear without a completion.
        std::terminate();
    }
    operation.posted.store(true, std::memory_order_release);
}

void CALLBACK TimerReady(void* context, BOOLEAN) { Post(*static_cast<Operation*>(context)); }

void Unregister(Operation& operation) noexcept {
    if (operation.wait) {
        if (!::UnregisterWaitEx(operation.wait, INVALID_HANDLE_VALUE)) {
            std::terminate();
        }
        operation.wait = nullptr;
    }
}

template <typename C>
void Advance(C* channel, DWORD bytes, DWORD error, bool empty) {
    constexpr bool file = std::is_same_v<C, fs::FileReadChannel> || std::is_same_v<C, fs::FileWriteChannel>;
    constexpr bool write = std::is_same_v<C, fs::FileWriteChannel> || std::is_same_v<C, net::TcpSendChannel>;
    auto& payload = channel->Submit();
    if constexpr (file) {
        if (!write && error == ERROR_HANDLE_EOF) error = ERROR_SUCCESS;
        if (error == ERROR_SUCCESS) payload.advance_offset(bytes);
    }
    std::size_t remaining = bytes;
    while (auto* submission = payload.NextSubmission()) {
        using Status = decltype(submission->status);
        if (error != ERROR_SUCCESS) {
            submission->status = Status::kError;
            submission->error_code = std::error_code(error, std::system_category());
        } else if constexpr (write) {
            const auto taken = (std::min)(remaining, submission->buffer.size());
            submission->bytes += taken;
            remaining -= taken;
            if (taken < submission->buffer.size()) {
                if (bytes == 0) {
                    submission->status = Status::kError;
                    submission->error_code = std::make_error_code(std::errc::io_error);
                } else {
                    submission->buffer = submission->buffer.subspan(taken);
                    break;
                }
            } else {
                submission->status = Status::kDone;
                submission->error_code = {};
            }
        } else {
            const auto taken = (std::min)(remaining, submission->buffer.size());
            submission->bytes = taken;
            remaining -= taken;
            submission->status = Status::kDone;
            submission->error_code = {};
        }
        payload.Complete();
        // Files submit only their first buffer, including at EOF or on error.
        if (file || empty || (error == ERROR_SUCCESS && bytes != 0 && remaining == 0)) break;
    }
    channel->Complete();
}
}  // namespace

struct IOCPMultiplexer::Impl {
    HANDLE port{};
    std::unordered_map<ChannelBase*, Operation*> channels;
    // Cancelled operations retain their OVERLAPPED until the packet is dequeued.
    std::unordered_map<OVERLAPPED*, std::unique_ptr<Operation>> operations;

    void Submit() {
        for (auto& [channel, pending] : channels) {
            if (pending) continue;
            auto operation = std::make_unique<Operation>(channel, port);
            auto* op = operation.get();
            if (channel->type() != ChannelType::kTimer) {
                std::visit(
                    [&](auto* typed) {
                        auto& payload = typed->Submit();
                        if (payload.size() == 0) return;
                        const auto& scatters = payload.header();
                        op->empty = scatters.front().count == 0;
                        const bool file =
                            channel->type() == ChannelType::kRead || channel->type() == ChannelType::kWrite;
                        std::size_t budget = (std::numeric_limits<DWORD>::max)();
                        for (const auto& scatter : scatters) {
                            const auto size = (std::min)(scatter.count, budget);
                            op->buffers.push_back(
                                WSABUF{static_cast<ULONG>(size), static_cast<char*>(scatter.buffers)});
                            budget -= size;
                            // Bound native counts and total bytes without truncating a submission.
                            if (file || op->empty || budget == 0 || op->buffers.size() == 64) break;
                        }
                        using C = std::remove_pointer_t<decltype(typed)>;
                        if constexpr (std::is_same_v<C, fs::FileReadChannel> ||
                                      std::is_same_v<C, fs::FileWriteChannel>) {
                            const auto offset = payload.offset();
                            op->Offset = static_cast<DWORD>(offset);
                            op->OffsetHigh = static_cast<DWORD>(offset >> 32);
                        }
                    },
                    AsDataChannel(channel));
                if (op->buffers.empty()) continue;
                op->hEvent = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
                if (!op->hEvent) {
                    throw std::system_error(GetLastError(), std::system_category(), "CreateEventW");
                }
            }
            operations.emplace(op, std::move(operation));
            pending = op;
            if (channel->type() == ChannelType::kTimer) {
                if (!::RegisterWaitForSingleObject(&op->wait, reinterpret_cast<HANDLE>(channel->native_handle()),
                                                   TimerReady, op, INFINITE, WT_EXECUTEONLYONCE)) {
                    const auto error = GetLastError();
                    pending = nullptr;
                    operations.erase(op);
                    throw std::system_error(error, std::system_category(), "RegisterWaitForSingleObject");
                }
                static_cast<time::TimerChannel*>(channel)->Submit().TakePoll();
                continue;
            }
            if (op->empty) {
                Post(*op);
                continue;
            }
            DWORD error = ERROR_SUCCESS;
            const auto handle = reinterpret_cast<HANDLE>(channel->native_handle());
            switch (channel->type()) {
                case ChannelType::kRead:
                    if (!::ReadFile(handle, op->buffers.front().buf, op->buffers.front().len, nullptr, op))
                        error = GetLastError();
                    break;
                case ChannelType::kWrite:
                    if (!::WriteFile(handle, op->buffers.front().buf, op->buffers.front().len, nullptr, op))
                        error = GetLastError();
                    break;
                case ChannelType::kReceive:
                    if (::WSARecv(channel->native_handle(), op->buffers.data(), static_cast<DWORD>(op->buffers.size()),
                                  nullptr, &op->flags, op, nullptr) == SOCKET_ERROR)
                        error = ::WSAGetLastError();
                    break;
                case ChannelType::kSend:
                    if (::WSASend(channel->native_handle(), op->buffers.data(), static_cast<DWORD>(op->buffers.size()),
                                  nullptr, 0, op, nullptr) == SOCKET_ERROR)
                        error = ::WSAGetLastError();
                    break;
                default:
                    std::terminate();
            }
            if (error != ERROR_SUCCESS && error != ERROR_IO_PENDING) Post(*op, error);
        }
    }

    bool Dispatch(DWORD timeout) {
        DWORD bytes{};
        ULONG_PTR key{};
        OVERLAPPED* overlapped{};
        const auto ok = ::GetQueuedCompletionStatus(port, &bytes, &key, &overlapped, timeout);
        const auto error = ok ? ERROR_SUCCESS : GetLastError();
        if (!overlapped) {
            if (error == WAIT_TIMEOUT) return false;
            throw std::system_error(error, std::system_category(), "GetQueuedCompletionStatus");
        }
        auto entry = operations.find(overlapped);
        if (entry == operations.end()) throw std::logic_error("unknown IOCP completion");
        auto operation = std::move(entry->second);
        operations.erase(entry);
        Unregister(*operation);
        auto* channel = operation->channel;
        if (!channel) return true;
        channels.at(channel) = nullptr;
        if (channel->type() == ChannelType::kTimer) {
            static_cast<time::TimerChannel*>(channel)->Complete();
        } else {
            std::visit(
                [&](auto* typed) {
                    Advance(typed, bytes, operation->synthetic ? operation->error : error, operation->empty);
                },
                AsDataChannel(channel));
        }
        return true;
    }
};

IOCPMultiplexer::IOCPMultiplexer() : Multiplexer(MultiplexerType::kIOCP), impl_(std::make_unique<Impl>()) {
    impl_->port = ::CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 1);
    if (!impl_->port) {
        throw std::system_error(GetLastError(), std::system_category(), "CreateIoCompletionPort");
    }
}

IOCPMultiplexer::~IOCPMultiplexer() noexcept {
    while (!impl_->channels.empty()) DeleteChannel(impl_->channels.begin()->first);
    while (!impl_->operations.empty()) impl_->Dispatch(INFINITE);
    ::CloseHandle(impl_->port);
}

void IOCPMultiplexer::Wait() {
    impl_->Submit();
    impl_->Dispatch(INFINITE);
    impl_->Submit();
}

void IOCPMultiplexer::WaitFor(std::chrono::milliseconds timeout) {
    impl_->Submit();
    const auto count = (std::clamp)(timeout.count(), std::chrono::milliseconds::rep{0},
                                    static_cast<std::chrono::milliseconds::rep>(INFINITE - 1));
    impl_->Dispatch(static_cast<DWORD>(count));
    impl_->Submit();
}

void IOCPMultiplexer::AddChannel(ChannelBase* channel) {
    if (!channel) throw std::invalid_argument("null IOCP channel");
    if (impl_->channels.contains(channel)) return;
    if (channel->type() != ChannelType::kTimer) {
        std::visit(
            [&](auto* typed) {
                auto& associated = [&]() -> std::uintptr_t& {
                    using C = std::remove_pointer_t<decltype(typed)>;
                    if constexpr (std::is_same_v<C, fs::FileReadChannel> || std::is_same_v<C, fs::FileWriteChannel>)
                        return typed->file_stream().completion_port_;
                    else
                        return typed->connector().completion_port_;
                }();
                const auto port = reinterpret_cast<std::uintptr_t>(impl_->port);
                if (associated == port) return;
                if (associated != 0) {
                    throw std::logic_error("native handle already belongs to another IOCP multiplexer");
                }
                if (!::CreateIoCompletionPort(reinterpret_cast<HANDLE>(channel->native_handle()), impl_->port, 0, 0)) {
                    throw std::system_error(GetLastError(), std::system_category(),
                                            "CreateIoCompletionPort: associate");
                }
                associated = port;
            },
            AsDataChannel(channel));
    }
    impl_->channels.emplace(channel, nullptr);
}

void IOCPMultiplexer::DeleteChannel(ChannelBase* channel) noexcept {
    auto entry = impl_->channels.find(channel);
    if (entry == impl_->channels.end()) return;
    auto* operation = entry->second;
    impl_->channels.erase(entry);
    if (!operation) return;
    operation->channel = nullptr;
    if (channel->type() == ChannelType::kTimer) {
        Unregister(*operation);
        static_cast<time::TimerChannel*>(channel)->Submit().ReleasePoll();
        if (!operation->posted.load(std::memory_order_acquire)) impl_->operations.erase(operation);
    } else if (!operation->synthetic) {
        const auto handle = reinterpret_cast<HANDLE>(channel->native_handle());
        if (!::CancelIoEx(handle, operation) && GetLastError() != ERROR_NOT_FOUND) std::terminate();
        // Deletion must stop kernel access to caller buffers, not just drop a packet.
        DWORD bytes{};
        if (!::GetOverlappedResult(handle, operation, &bytes, TRUE) && GetLastError() == ERROR_IO_INCOMPLETE)
            std::terminate();
    }
}
}  // namespace nbio::core
#endif