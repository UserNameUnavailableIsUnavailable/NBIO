#pragma once

#if defined(__linux__)
#include <sys/uio.h>
#endif

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <nbio/core/scatter.hpp>
#include <nbio/fs/result.hpp>
#include <vector>

namespace nbio::fs::detail {
using core::Scatter;

template <typename C>
class IOVectorPayload {
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
    std::vector<::iovec>& native_header() {
        const auto& scatters = header();
        native_vectors_.clear();
        for (const auto& scatter : scatters) {
            native_vectors_.push_back(::iovec{scatter.buffers, scatter.count});
        }
        return native_vectors_;
    }
#endif

    std::uint64_t offset() const noexcept { return offset_; }
    void set_offset(std::uint64_t offset) noexcept { offset_ = offset; }
    void advance_offset(std::size_t bytes) noexcept { offset_ += static_cast<std::uint64_t>(bytes); }
    std::size_t size() const noexcept { return submitted_.size() + prepared_.size(); }

    void Submit(Transmission* transmission) {
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

    void Conclude() noexcept {
        assert(!completed_.empty());
        completed_.pop_front();
    }

private:
    std::uint64_t offset_{0};
    std::vector<Scatter> vectors_;
#if defined(__linux__)
    std::vector<::iovec> native_vectors_;
#endif
    std::deque<Transmission*> prepared_{};
    std::deque<Transmission*> submitted_{};
    std::deque<Transmission*> completed_{};
};
}  // namespace nbio::fs::detail