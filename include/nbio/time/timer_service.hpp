#pragma once

#include <chrono>
#include <nbio/runtime/daemon.hpp>
#include <nbio/async/task.hpp>

namespace nbio::time {
class SystemTimeService final {
   public:
    SystemTimeService() noexcept = default;

    nbio::async::Task<void> Sleep(std::chrono::steady_clock::duration duration) const {
        return SleepUntil(std::chrono::steady_clock::now() + duration);
    }

    nbio::async::Task<void> Sleep(std::chrono::steady_clock::time_point point) const { return SleepUntil(point); }

   private:
    static nbio::async::Task<void> SleepUntil(std::chrono::steady_clock::time_point point) {
        co_await nbio::runtime::Daemon::timer_channel().Sleep(point);
    }
};
}  // namespace nbio::time
