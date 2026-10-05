#include <NBIO/Notification/ConditionVariable.hpp>

#include <NBIO/Async/Coroutine.hpp>
#include <algorithm>
#include <atomic>
#include <utility>

#include <NBIO/Async/Runtime.hpp>

namespace NBIO::Notification {
ConditionVariable::ConditionVariable() : channel_(NBIO::Async::Runtime::notify_channel()) {}

ConditionVariable::~ConditionVariable() noexcept {
    std::lock_guard lock(mutex_);
    assert(notifiees_.empty() && "destroying ConditionVariable while waiters still exist");
}

void ConditionVariable::NotifyOne() {
    std::lock_guard lock(mutex_);
    while (!notifiees_.empty()) {
        Async::Coroutine waiter = std::move(notifiees_.front());
        notifiees_.pop_front();
        if (!waiter) {
            continue;
        }
        channel_.Park(std::move(waiter));
        return;
    }
    stock_++;
}

void ConditionVariable::NotifyAll() {
    broadcasting_.store(true, std::memory_order_release);  // all incoming waits can return immediately
    {
        std::lock_guard lock(mutex_);
        auto end = std::remove_if(notifiees_.begin(), notifiees_.end(),
                                  [](const Async::Coroutine& coroutine) { return !coroutine; });
        if (end != notifiees_.begin()) {
            channel_.Park(notifiees_.begin(), end);
        }
        notifiees_.clear();
    }
    broadcasting_.store(false, std::memory_order_release);
}
}  // namespace NBIO::Notification



