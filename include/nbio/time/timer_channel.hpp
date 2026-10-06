#pragma once

#include <chrono>
#include <coroutine>
#include <nbio/async/coroutine.hpp>
#include <nbio/async/scheduler.hpp>
#include <nbio/async/task.hpp>
#include <nbio/core/channel.hpp>
#include <nbio/core/types.hpp>
#include <nbio/time/timer.hpp>
#include <nbio/utility/priority_queue.hpp>
#include <queue>

namespace nbio::time {
namespace detail {
template <typename C>
class PollPayload {
   public:
    bool WantsPoll() const noexcept { return !submitted_; }
    void TakePoll() noexcept { submitted_ = true; }
    void ReleasePoll() noexcept { submitted_ = false; }
    bool Outstanding() const noexcept { return submitted_; }

   private:
    bool submitted_{false};
};
}  // namespace detail

class TimerChannel;
namespace detail {

struct TimerEntry {
    // std::coroutine_handle<> coroutine_view{};
    async::Coroutine coroutine;
    std::chrono::steady_clock::time_point timepoint{};
};
// Stateless comparator functor: operator() can be inlined, avoiding the
// indirect-call overhead of a function pointer in priority_queue operations.
struct TimerEntryComparator {
    bool operator()(const TimerEntry& a, const TimerEntry& b) const { return a.timepoint > b.timepoint; }
};
struct SleepAwaiter {
    TimerChannel& channel;
    std::chrono::steady_clock::time_point due;

    SleepAwaiter(TimerChannel& channel, std::chrono::steady_clock::time_point due) noexcept
        : channel(channel), due(due) {}

    SleepAwaiter(const SleepAwaiter&) = delete;
    SleepAwaiter& operator=(const SleepAwaiter&) = delete;

    ~SleepAwaiter() = default;

    bool await_ready() const noexcept { return due <= std::chrono::steady_clock::now(); }

    template <typename PromiseType>
    void await_suspend(std::coroutine_handle<PromiseType> handle);

    void await_resume() noexcept {}
};
}  // namespace detail

// TimerChannel waits for a dedicated timer to fire.
// In the event handler, the channel pops several due entries, each entry containing a coroutine.
class TimerChannel final : public nbio::core::Channel<TimerChannel> {
    friend struct detail::SleepAwaiter;

   public:
    using Handle = time::Timer::Handle;
    using Payload = detail::PollPayload<TimerChannel>;

    explicit TimerChannel(nbio::time::Timer& timer, nbio::core::Multiplexer& multiplexer,
                                nbio::async::Scheduler& scheduler);
    ~TimerChannel() noexcept;

    nbio::time::Timer& timer() noexcept { return timer_; }
    const nbio::time::Timer& timer() const noexcept { return timer_; }

    // The operation this channel wants from the backend is a one-shot poll; the
    // payload carries only whether one is already out there.
    Payload& Submit();
    void Complete();

    // Sleep 原语：挂起当前协程直到 due，定时器触发后由调度器恢复。
    // 返回的 awaiter 直接 co_await 即可。
    detail::SleepAwaiter Sleep(std::chrono::steady_clock::time_point due) noexcept {
        return detail::SleepAwaiter{*this, due};
    }

   private:
    void Park(async::Coroutine coroutine_view, std::chrono::steady_clock::time_point due);
    bool remove(async::Coroutine coroutine_view);

    nbio::time::Timer& timer_;
    std::queue<async::Coroutine> immediate_queue_;  // already-timeout coroutines
    nbio::core::PriorityQueue<detail::TimerEntry, detail::TimerEntryComparator> queue_;
    Payload payload_{};
};

template <typename PromiseType>
inline void detail::SleepAwaiter::await_suspend(std::coroutine_handle<PromiseType> handle) {
    auto coroutine = async::Coroutine::FromHandle(handle);
    channel.Arm();
    channel.Park(std::move(coroutine), due);
}
}  // namespace nbio::time
