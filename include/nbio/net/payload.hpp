#pragma once

#if defined(__linux__)
#include <sys/socket.h>
#include <sys/uio.h>
#endif

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <nbio/core/scatter.hpp>
#include <nbio/net/result.hpp>
#include <system_error>
#include <variant>
#include <vector>

namespace nbio::net::detail {
using core::Scatter;

template <typename C>
class PollPayload {
public:
    bool WantsPoll() const noexcept { return !submitted_; }
    void TakePoll() noexcept { submitted_ = true; }
    void ReleasePoll() noexcept { submitted_ = false; }
    bool outstanding() const noexcept { return submitted_; }

private:
    bool submitted_{false};
};

template <typename C>
class CommunicationPayload {
public:
    void bundle() {
        submitted_.insert(submitted_.end(), prepared_.begin(), prepared_.end());
        prepared_.clear();
    }

    std::size_t size() const noexcept { return prepared_.size() + submitted_.size(); }

    void submit(Communication* communication) {
        assert(communication != nullptr);
        prepared_.push_back(communication);
    }

    Communication* NextSubmission() { return submitted_.empty() ? nullptr : submitted_.front(); }

    void Complete() noexcept {
        assert(!submitted_.empty());
        completed_.push_back(submitted_.front());
        submitted_.pop_front();
    }

    Communication* NextCompletion() { return completed_.empty() ? nullptr : completed_.front(); }

    void conclude() noexcept {
        assert(!completed_.empty());
        completed_.pop_front();
    }

private:
    std::deque<Communication*> prepared_{};
    std::deque<Communication*> submitted_{};
    std::deque<Communication*> completed_{};
};

template <typename C>
class MessagePayload {
public:
    std::vector<Scatter>& header() {
        submitted_.insert(submitted_.end(), prepared_.begin(), prepared_.end());
        prepared_.clear();
        vectors_.clear();
        for (auto* trans : submitted_) {
            vectors_.push_back(Scatter{trans->buffer.data(), trans->buffer.size()});
        }
        return vectors_;
    }

#if defined(__linux__)
    ::msghdr& native_header() {
        const auto& scatters = header();
        native_vectors_.clear();
        for (const auto& scatter : scatters) {
            native_vectors_.push_back(::iovec{scatter.buffers, scatter.count});
        }
        header_.msg_iov = native_vectors_.data();
        header_.msg_iovlen = native_vectors_.size();
        return header_;
    }
#endif

    std::size_t size() const noexcept { return submitted_.size() + prepared_.size(); }

    void submit(Transmission* transmission) {
        assert(transmission != nullptr);
        prepared_.push_back(transmission);
    }

    Transmission* NextSubmission() { return submitted_.empty() ? nullptr : submitted_.front(); }

    void Complete() noexcept {
        assert(!submitted_.empty());
        completed_.push_back(submitted_.front());
        submitted_.pop_front();
    }

    Transmission* NextCompletion() { return completed_.empty() ? nullptr : completed_.front(); }

    void conclude() noexcept {
        assert(!completed_.empty());
        completed_.pop_front();
    }

private:
    std::vector<Scatter> vectors_;
#if defined(__linux__)
    ::msghdr header_{};
    std::vector<::iovec> native_vectors_;
#endif
    std::deque<Transmission*> prepared_{};
    std::deque<Transmission*> submitted_{};
    std::deque<Transmission*> completed_{};
};
}  // namespace nbio::net::detail