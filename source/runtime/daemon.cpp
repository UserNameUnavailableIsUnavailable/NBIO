#include <chrono>
#include <memory>
#include <nbio/runtime/daemon.hpp>
#include <nbio/core/epoll_multiplexer.hpp>
#include <stdexcept>
#include <thread>
#include <utility>
#include "nbio/notification/notifier.hpp"
#include "nbio/time/timer.hpp"
#include "nbio/time/timer_channel.hpp"

namespace nbio::runtime {
thread_local std::unique_ptr<Daemon> Daemon::daemon_{};

Daemon::Daemon(Init init)
    : multiplexer_(std::move(init.multiplexer))
{
}

Daemon::~Daemon() noexcept = default;

std::function<void(bool)> Daemon::make_idle_hook(core::Multiplexer& multiplexer) {
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

void Daemon::Initialize(std::unique_ptr<core::Multiplexer> multiplexer) {
    if (is_initialized()) {
        throw std::runtime_error("nbio backend already initialized on this thread");
    }
    Init init{.multiplexer = std::move(multiplexer)};
    daemon_ = std::make_unique<Daemon>(std::move(init));
}

Daemon& Daemon::instance() {
    if (!is_initialized()) {
        Initialize(std::make_unique<core::EpollMultiplexer>());
    }
    return *daemon_;
}

nbio::async::Scheduler& Daemon::scheduler()
{
	auto& self = instance();
	if (!self.scheduler_) [[unlikely]] {
		self.scheduler_ = std::make_unique<nbio::async::Scheduler>(
			make_idle_hook(multiplexer())
		);
	}
	return *self.scheduler_;
}

core::Multiplexer& Daemon::multiplexer() {
	auto& self = instance();
	if (!self.multiplexer_) [[unlikely]] {
		self.multiplexer_ = make_default_multiplexer();
	}
	return *self.multiplexer_;
}

notification::NotifierChannel& Daemon::notifier_channel() {
	auto& self = instance();
	if (!self.notifier_channel_) [[unlikely]] {
		self.notifier_ = std::make_unique<nbio::notification::Notifier>();
		self.notifier_channel_ = std::make_unique<nbio::notification::NotifierChannel>(
            *self.notifier_,
			multiplexer(),
			scheduler()
		);
	}
	return *self.notifier_channel_;
}

time::TimerChannel& Daemon::timer_channel() {
	auto& self = instance();
	if (!self.timer_channel_) [[unlikely]] {
		self.timer_ = std::make_unique<nbio::time::Timer>();
		self.timer_channel_ = std::make_unique<nbio::time::TimerChannel>(
			*self.timer_,
			multiplexer(),
			scheduler()
		);
	}
	return *self.timer_channel_;
}

signal::SystemSignalChannel& Daemon::signal_channel() {
    auto& self = instance();
    if (!self.signal_channel_) [[unlikely]] {
        self.signal_ = std::make_unique<nbio::signal::SystemSignal>();
        self.signal_channel_ = std::make_unique<signal::SystemSignalChannel>(
			*self.signal_,
			multiplexer(),
			scheduler()
		);
    }
    return *self.signal_channel_;
}

std::unique_ptr<core::Multiplexer> make_default_multiplexer() { return std::make_unique<core::EpollMultiplexer>(); }
}  // namespace nbio::runtime
