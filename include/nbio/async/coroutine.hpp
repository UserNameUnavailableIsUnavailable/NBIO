#pragma once

#include <atomic>
#include <cassert>
#include <coroutine>
#include <list>
#include <memory>

namespace nbio::async {
class Scheduler;
struct CoroutineControlBlock;

enum class CoroutineLifecycle {
    kAlive,
    kFinished,
    kCancelled,
};

enum class CoroutineJoinStatus {
    kCompleted,  // the root reached its final suspend point
    kCancelled,  // the tree was cancelled first
};

struct Coroutine {
    std::coroutine_handle<> handle{};

    std::weak_ptr<CoroutineControlBlock> control_block{};

    explicit operator bool() const noexcept;
    bool operator==(const Coroutine& other) const noexcept { return other.handle == handle; }

    std::shared_ptr<CoroutineControlBlock> Lock() const noexcept { return control_block.lock(); }

    template <typename Promise>
    static Coroutine FromHandle(std::coroutine_handle<Promise> handle) noexcept {
        assert(handle.promise().control_block != nullptr && "a coroutine outside a Spawn tree cannot be Parked");
        return Coroutine{handle, handle.promise().control_block->weak_from_this()};
    }
};

// Coroutines can be chained. Coroutine control block manages the lifecycle of a coroutine chain.
struct CoroutineControlBlock : std::enable_shared_from_this<CoroutineControlBlock> {
    std::coroutine_handle<> root{};  // the root frame: destroying the block destroys the whole tree
    Scheduler* scheduler{nullptr};
    std::list<std::shared_ptr<CoroutineControlBlock>>::iterator index;  // where the registry holds this block
    std::atomic<CoroutineLifecycle> state{CoroutineLifecycle::kAlive};
    Coroutine join{};           // the coroutine Parked on `co_await token`, if any
    CoroutineJoinStatus* join_status{};  // where that joiner reads the outcome from, inside its own frame

    bool is_cancelled() const noexcept { return state.load(std::memory_order_acquire) == CoroutineLifecycle::kCancelled; }
    bool is_finished() const noexcept { return state.load(std::memory_order_acquire) == CoroutineLifecycle::kFinished; }
    bool is_dead() const noexcept { return state.load(std::memory_order_acquire) != CoroutineLifecycle::kAlive; }

    void Cancel() noexcept;
    void Finish() noexcept;

    ~CoroutineControlBlock() noexcept;
};

class CoroutineJoinHandle {
   public:
    CoroutineJoinHandle() noexcept = default;
    explicit CoroutineJoinHandle(const std::shared_ptr<CoroutineControlBlock>& control_block) noexcept
        : coroutine_control_block_(control_block) {}

    void Cancel();
    bool is_dead() const noexcept;

    struct JoinAwaiter {
        std::weak_ptr<CoroutineControlBlock> block;
        CoroutineJoinStatus outcome{CoroutineJoinStatus::kCompleted};  // written by the scheduler before it wakes us

        // A joiner destroyed while still Parked -- its own tree was cancelled -- has to
        // take its name off the block it Parked on, or the scheduler would wake a frame
        // that is no longer there.
        ~JoinAwaiter() noexcept {
            const std::shared_ptr<CoroutineControlBlock> alive = block.lock();
            if (alive != nullptr && alive->join_status == &outcome) {
                alive->join = Coroutine{};
                alive->join_status = nullptr;
            }
        }

        bool await_ready() const noexcept {
            const std::shared_ptr<CoroutineControlBlock> alive = block.lock();
            // Gone, or finished. A *cancelled* block whose removal has not been drained
            // yet is deliberately not ready: Parking on it means being told kCancelled,
            // where answering here could only guess kCompleted.
            return !alive || alive->is_finished();
        }
        template <typename Promise>
        bool await_suspend(std::coroutine_handle<Promise> caller) noexcept {
            const std::shared_ptr<CoroutineControlBlock> alive = block.lock();
            if (!alive) {
                return false;  // over between the two questions: do not Park for it
            }
            alive->join = Coroutine::FromHandle(caller);  // resumed by the scheduler when the tree is over
            alive->join_status = &outcome;
            return true;
        }
        CoroutineJoinStatus await_resume() const noexcept { return outcome; }
    };

    JoinAwaiter operator co_await() const noexcept { return JoinAwaiter{coroutine_control_block_}; }

   private:
    // TODO: Should we make this a shared_ptr? With weak_ptr, one cannot know the exact lifetime of the coroutine once the control block is gone.
    std::weak_ptr<CoroutineControlBlock> coroutine_control_block_;
};

inline Coroutine::operator bool() const noexcept {
    // The block first, and through the weak reference: the frame is destroyed with the
    // block, so asking the handle anything before asking whether the coroutine is still
    // live would be reading a frame that may be gone. `handle` alone is free and rules
    // out the default-constructed case.
    if (!handle) {
        return false;
    }
    const std::shared_ptr<CoroutineControlBlock> block = control_block.lock();
    return block && !block->is_dead() && !handle.done();
}

}  // namespace nbio::async
