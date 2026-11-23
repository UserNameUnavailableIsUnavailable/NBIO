#include <cerrno>
#include <chrono>
#include <cstring>
#include <nbio/time/timer.hpp>
#include <stdexcept>
#include <system_error>

#if defined(__linux__)
#elif defined(_WIN32)
#include <Windows.h>

namespace nbio::time {
Timer::Timer() {
    auto handle = ::CreateWaitableTimerW(nullptr, TRUE, nullptr);
    if (!handle) {
        throw std::system_error(GetLastError(), std::system_category(), "CreateWaitableTimerW");
    }
    handle_ = reinterpret_cast<std::uintptr_t>(handle);
}

Timer::~Timer() noexcept { ::CloseHandle(reinterpret_cast<HANDLE>(handle_)); }

void Timer::NonBlocking(bool enabled) { non_blocking_ = enabled; }

void Timer::FireAfter(std::chrono::nanoseconds duration) {
    using Tick = std::chrono::duration<std::int64_t, std::ratio<1, 10000000>>;
    const auto ticks = std::chrono::ceil<Tick>(duration).count();
    LARGE_INTEGER due{};
    due.QuadPart = -(ticks > 0 ? ticks : 1);
    if (!::SetWaitableTimer(reinterpret_cast<HANDLE>(handle_), &due, 0, nullptr, nullptr, FALSE)) {
        throw std::system_error(GetLastError(), std::system_category(), "SetWaitableTimer");
    }
    active_ = true;
}

void Timer::fire_at(Timepoint timepoint) { fire_after(timepoint - Clock::now()); }

void Timer::cancel() {
    if (!::CancelWaitableTimer(reinterpret_cast<HANDLE>(handle_))) {
        throw std::system_error(GetLastError(), std::system_category(), "CancelWaitableTimer");
    }
    active_ = false;
}

utility::expected<void, std::error_code> Timer::wait() {
    if (!active_) {
        return utility::unexpected(std::make_error_code(std::errc::operation_would_block));
    }
    const auto result = ::WaitForSingleObject(reinterpret_cast<HANDLE>(handle_), non_blocking_ ? 0 : INFINITE);
    if (result == WAIT_OBJECT_0) {
        active_ = false;
        return {};
    }
    if (result == WAIT_TIMEOUT) {
        return utility::unexpected(std::make_error_code(std::errc::operation_would_block));
    }
    return utility::unexpected(std::error_code(GetLastError(), std::system_category()));
}
}  // namespace nbio::time
#endif

#if defined(_WIN32)
#endif

#if defined(__linux__)
#include <fcntl.h>
#include <sys/timerfd.h>
#include <unistd.h>

namespace nbio::time {
Timer::Timer() {
    auto handle = timerfd_create(CLOCK_MONOTONIC, 0);
    if (handle == -1) {
        throw std::runtime_error("failed to create timer");
    }
    handle_ = static_cast<std::uintptr_t>(handle);
}

void Timer::NonBlocking(bool enabled) {
    int flags = fcntl(handle_, F_GETFL, 0);
    if (flags == -1) {
        throw std::runtime_error("failed to get file flags");
    }
    if (enabled) {
        flags |= O_NONBLOCK;
    } else {
        flags &= ~O_NONBLOCK;
    }
    if (fcntl(handle_, F_SETFL, flags) == -1) {
        throw std::runtime_error("failed to set file flags");
    }
}

Timer::~Timer() noexcept { ::close(handle_); }

void Timer::fire_at(std::chrono::steady_clock::time_point timepoint) {
    auto now = std::chrono::steady_clock::now();
    auto duration = timepoint - now;
    if (duration.count() < 0) {
        duration = std::chrono::nanoseconds(1);
    }
    fire_after(duration);
}

void Timer::cancel()
#if defined(__linux__)
{
    itimerspec spec;
    std::memset(&spec, 0, sizeof(spec));
    timerfd_settime(handle_, 0, &spec, nullptr);
}

utility::expected<void, std::error_code> Timer::wait() {
    std::uint64_t expirations = 0;
    while (true) {
        const auto n = ::read(handle_, &expirations, sizeof(expirations));
        if (n > 0) {
            return {};
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return utility::unexpected<std::error_code>(std::make_error_code(std::errc::operation_would_block));
        }
        if (errno == EINTR) {
            continue;
        }
        return utility::unexpected<std::error_code>(std::error_code(errno, std::system_category()));
    }
}
#endif
}  // namespace nbio::time
#endif
