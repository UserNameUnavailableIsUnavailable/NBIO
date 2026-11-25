#include <atomic>
#include <nbio/async/coroutine.hpp>
#include <nbio/async/scheduler.hpp>

namespace nbio::async {
void CoroutineControlBlock::Cancel() noexcept {
    auto expected{CoroutineLifecycle::kAlive};
    if (state.compare_exchange_strong(expected, CoroutineLifecycle::kCancelled, std::memory_order_acq_rel)) {
        if (pending_io_ == 0) scheduler->Reclaim(*this);
    }
}

void CoroutineControlBlock::Finish() noexcept {
    auto expected{CoroutineLifecycle::kAlive};
    if (state.compare_exchange_strong(expected, CoroutineLifecycle::kFinished, std::memory_order_acq_rel)) {
        if (pending_io_ == 0) scheduler->Reclaim(*this);
    }
}

void CoroutineControlBlock::PinIO() noexcept {
    assert(!is_dead());
    ++pending_io_;
}

void CoroutineControlBlock::UnpinIO() noexcept {
    assert(pending_io_ != 0);
    if (--pending_io_ == 0 && is_dead()) scheduler->Reclaim(*this);
}

CoroutineControlBlock::~CoroutineControlBlock() noexcept {
    if (auto handle = std::exchange(root, {})) {
        handle.destroy();
    }
}

void CoroutineJoinHandle::Cancel() {
    if (const std::shared_ptr<CoroutineControlBlock> ccb = coroutine_control_block_.lock()) {
        ccb->Cancel();
    }
}

bool CoroutineJoinHandle::is_dead() const noexcept {
    const std::shared_ptr<CoroutineControlBlock> ccb = coroutine_control_block_.lock();
    return !ccb || ccb->is_dead();
}
}  // namespace nbio::async
