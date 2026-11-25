#pragma once

#if defined(__linux__)
#include <limits.h>
#include <sys/uio.h>
#endif

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <nbio/core/scatter.hpp>
#include <nbio/fs/result.hpp>
#include <system_error>
#include <vector>

namespace nbio::fs::detail {
using core::Scatter;

template <typename C>
class IOVectorPayload {
public:
    std::vector<Scatter>& header(std::size_t limit = kMaxVectors) {
        if (submitted_.empty() && !prepared_.empty()) {
            submitted_.push_back(prepared_.front());
            prepared_.pop_front();
        }
        const auto batch_limit = (std::min)({limit, kMaxVectors, submission_limit_});
        while (!prepared_.empty() && submitted_.size() < batch_limit) {
            const auto* previous = submitted_.back();
            if (previous->offset + previous->buffer.size() != prepared_.front()->offset) break;
            submitted_.push_back(prepared_.front());
            prepared_.pop_front();
        }
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

    std::uint64_t offset() const noexcept {
        assert(!submitted_.empty());
        return submitted_.front()->offset;
    }
    std::size_t size() const noexcept { return submitted_.size() + prepared_.size(); }

    void Submit(Transmission* transmission) {
        assert(transmission != nullptr);
        prepared_.push_back(transmission);
    }

    void UndoSubmit(Transmission* transmission) noexcept {
        assert(!prepared_.empty() && prepared_.back() == transmission);
        prepared_.pop_back();
    }

    void SetSubmissionLimit(std::size_t limit) noexcept {
        assert(limit != 0);
        submission_limit_ = limit;
    }

    Transmission* NextSubmission() { return submitted_.empty() ? nullptr : submitted_.front(); }

    void Complete() noexcept {
        assert(!submitted_.empty());
        completed_.push_back(submitted_.front());
        submitted_.pop_front();
    }

    void CompleteRead(std::size_t bytes) {
        while (auto* transmission = NextSubmission()) {
            const auto taken = (std::min)(bytes, transmission->buffer.size());
            transmission->bytes = taken;
            transmission->status = OperationStatus::kDone;
            transmission->error_code = {};
            bytes -= taken;
            Complete();
        }
    }

    void CompleteWrite(std::size_t bytes) {
        const auto transferred = bytes;
        while (auto* transmission = NextSubmission()) {
            const auto size = transmission->buffer.size();
            if (size != 0 && bytes == 0) {
                if (transferred == 0) FailSubmitted(std::make_error_code(std::errc::io_error));
                break;
            }
            const auto taken = (std::min)(bytes, size);
            transmission->bytes += taken;
            transmission->offset += taken;
            bytes -= taken;
            if (taken != size) {
                transmission->buffer = transmission->buffer.subspan(taken);
                break;
            }
            transmission->status = OperationStatus::kDone;
            transmission->error_code = {};
            Complete();
        }
    }

    void FailSubmitted(const std::error_code& error) {
        while (auto* transmission = NextSubmission()) {
            transmission->status = OperationStatus::kError;
            transmission->error_code = error;
            Complete();
        }
    }

    void FailQueued(const std::error_code& error) {
        FailSubmitted(error);
        while (!prepared_.empty()) {
            auto* transmission = prepared_.front();
            prepared_.pop_front();
            transmission->status = OperationStatus::kError;
            transmission->error_code = error;
            completed_.push_back(transmission);
        }
    }

    Transmission* NextCompletion() { return completed_.empty() ? nullptr : completed_.front(); }

    void Conclude() noexcept {
        assert(!completed_.empty());
        completed_.pop_front();
    }

private:
#if defined(IOV_MAX)
    static constexpr std::size_t kMaxVectors = IOV_MAX;
#else
    static constexpr std::size_t kMaxVectors = 16;
#endif
    std::vector<Scatter> vectors_;
#if defined(__linux__)
    std::vector<::iovec> native_vectors_;
#endif
    std::deque<Transmission*> prepared_{};
    std::deque<Transmission*> submitted_{};
    std::deque<Transmission*> completed_{};
    std::size_t submission_limit_{kMaxVectors};
};
}  // namespace nbio::fs::detail