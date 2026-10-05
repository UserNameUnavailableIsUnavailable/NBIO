#include <nbio/async/runtime.hpp>
#include <nbio/core/epoll_multiplexer.hpp>
#include <chrono>
#include <stdexcept>
#include <utility>

#if !defined(__linux__)
#error "nbio is only implemented for Linux"
#endif

#include <nbio/core/types.hpp>

namespace nbio::async {
thread_local std::unique_ptr<Runtime> Runtime::runtime_{};

Runtime::Runtime(Init init)
    : multiplexer_(std::move(init.multiplexer)),
      scheduler_(make_idle_hook(*multiplexer_)),
      timer_channel_(timer_, *multiplexer_, scheduler_),
      notify_channel_(notifier_, *multiplexer_, scheduler_)
{
}

Runtime::~Runtime() noexcept = default;

std::function<void(bool)> Runtime::make_idle_hook(core::Multiplexer& multiplexer) {
    // The scheduler Parks here whenever it has nothing to run: the multiplexer
    // *is* the idle coroutine.
    return [&multiplexer](bool blocking) {
        if (blocking) {
            multiplexer.Run();
        } else {
            multiplexer.RunFor(std::chrono::milliseconds(0));
        }
    };
}

void Runtime::Initialize(std::unique_ptr<core::Multiplexer> multiplexer) {
    if (is_initialized()) {
        throw std::runtime_error("nbio backend already initialized on this thread");
    }
    Init init{.multiplexer = std::move(multiplexer)};
    runtime_ = std::make_unique<Runtime>(std::move(init));
}

Runtime& Runtime::instance() {
    if (!is_initialized()) {
        Initialize(std::make_unique<core::EpollMultiplexer>());
    }
    return *runtime_;
}

nbio::async::Scheduler& Runtime::scheduler() { return instance().scheduler_; }

core::Multiplexer& Runtime::multiplexer() { return *instance().multiplexer_; }

notification::EventNotifyChannel& Runtime::notify_channel() { return instance().notify_channel_; }

time::SystemTimerChannel& Runtime::timer_channel() { return instance().timer_channel_; }

signal::SystemSignalChannel& Runtime::signal_channel() {
    auto& self = instance();
    if (!self.signal_channel_) {
        self.signal_ = std::make_unique<nbio::signal::SystemSignal>();
        self.signal_channel_ =
            std::make_unique<signal::SystemSignalChannel>(*self.signal_, *self.multiplexer_, self.scheduler_);
    }
    return *self.signal_channel_;
}

std::unique_ptr<core::Multiplexer> make_default_multiplexer() { return std::make_unique<core::EpollMultiplexer>(); }
}  // namespace nbio::async
