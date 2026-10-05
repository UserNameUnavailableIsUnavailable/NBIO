#include <NBIO/Async/Runtime.hpp>
#include <NBIO/Core/EpollMultiplexer.hpp>
#include <chrono>
#include <stdexcept>
#include <utility>

#if !defined(__linux__)
#error "NBIO is only implemented for Linux"
#endif

#include <NBIO/Core/Types.hpp>

namespace NBIO::Async {
thread_local std::unique_ptr<Runtime> Runtime::runtime_{};

Runtime::Runtime(Init init)
    : multiplexer_(std::move(init.multiplexer)),
      scheduler_(make_idle_hook(*multiplexer_)),
      timer_channel_(timer_, *multiplexer_, scheduler_),
      notify_channel_(notifier_, *multiplexer_, scheduler_)
{
}

Runtime::~Runtime() noexcept = default;

std::function<void(bool)> Runtime::make_idle_hook(Core::Multiplexer& multiplexer) {
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

void Runtime::initialize(std::unique_ptr<Core::Multiplexer> multiplexer) {
    if (is_initialized()) {
        throw std::runtime_error("NBIO backend already initialized on this thread");
    }
    Init init{.multiplexer = std::move(multiplexer)};
    runtime_ = std::make_unique<Runtime>(std::move(init));
}

Runtime& Runtime::instance() {
    if (!is_initialized()) {
        initialize(std::make_unique<Core::EpollMultiplexer>());
    }
    return *runtime_;
}

NBIO::Async::Scheduler& Runtime::scheduler() { return instance().scheduler_; }

Core::Multiplexer& Runtime::multiplexer() { return *instance().multiplexer_; }

Notification::EventNotifyChannel& Runtime::notify_channel() { return instance().notify_channel_; }

Time::SystemTimerChannel& Runtime::timer_channel() { return instance().timer_channel_; }

Signal::SystemSignalChannel& Runtime::signal_channel() {
    auto& self = instance();
    if (!self.signal_channel_) {
        self.signal_ = std::make_unique<NBIO::Signal::SystemSignal>();
        self.signal_channel_ =
            std::make_unique<Signal::SystemSignalChannel>(*self.signal_, *self.multiplexer_, self.scheduler_);
    }
    return *self.signal_channel_;
}

std::unique_ptr<Core::Multiplexer> make_default_multiplexer() { return std::make_unique<Core::EpollMultiplexer>(); }
}  // namespace NBIO::Async
