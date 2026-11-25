#include <cassert>
#include <nbio/fs/file_stream.hpp>

namespace nbio::fs {
void FileStream::Enqueue(Direction direction) {
    if (!queue_.empty() && queue_.back().direction == direction) {
        ++queue_.back().size;
    } else {
        queue_.push_back(Group{direction, 1});
    }
}

bool FileStream::CanSubmit(Direction direction) const noexcept {
    return !queue_.empty() && queue_.front().direction == direction;
}

std::size_t FileStream::SubmissionLimit(Direction direction) const noexcept {
    assert(CanSubmit(direction));
    return queue_.front().size;
}

void FileStream::Complete(Direction direction) {
    auto complete_one = [this](Direction current) {
        auto* completion = current == Direction::kRead ? read_channel_->payload_.NextCompletion()
                                                       : write_channel_->payload_.NextCompletion();
        assert(completion != nullptr);
        if (completion->status == OperationStatus::kError && !error_) error_ = completion->error_code;
        if (current == Direction::kRead)
            read_channel_->CompleteOne();
        else
            write_channel_->CompleteOne();
        assert(!queue_.empty() && queue_.front().direction == current);
        if (--queue_.front().size == 0) queue_.pop_front();
    };

    while (CanSubmit(direction)) {
        const auto* completion = direction == Direction::kRead ? read_channel_->payload_.NextCompletion()
                                                                : write_channel_->payload_.NextCompletion();
        if (!completion) break;
        complete_one(direction);
        if (error_) break;
    }
    if (error_) {
        read_channel_->payload_.FailQueued(error_);
        write_channel_->payload_.FailQueued(error_);
        while (!queue_.empty()) complete_one(queue_.front().direction);
    }
    if (read_channel_->is_idle()) read_channel_->Disarm();
    if (write_channel_->is_idle()) write_channel_->Disarm();
}
}  // namespace nbio::fs
