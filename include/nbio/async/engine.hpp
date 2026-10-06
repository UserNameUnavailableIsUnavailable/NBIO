#pragma once

#include <functional>
#include <memory>
#include <nbio/async/scheduler.hpp>
#include <nbio/core/system_signal.hpp>
#include <nbio/core/timer.hpp>
#include <nbio/core/types.hpp>
#include <nbio/notification/notifier.hpp>
#include <nbio/notification/notifier_channel.hpp>
#include <nbio/signal/system_signal_channel.hpp>
#include <nbio/time/timer_channel.hpp>

#include "types.hpp"

namespace nbio::runtime::Daemon {
class Engine {
   public:
    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;
    Engine(Engine&&) = delete;
    Engine& operator=(Engine&&) = delete;

    ~Engine() noexcept;

    // Installs a backend on the current thread. Throws if one is already
    // installed there.
    static void initialize(std::unique_ptr<Multiplexer> multiplexer);

    // Whether a backend has been installed on this thread.
    static bool is_initialized() { return static_cast<bool>(engine_); }

    // The engine current on this thread. Throws when installed() is false.
    static Engine& instance();

    // ---- what nbio::async asks of a Daemon tag ----
    static nbio::async::Scheduler& scheduler();
    static Multiplexer& multiplexer();

    // The channel carrying application-generated events. ConditionVariable
    // binds to it, so an application event travels the same path as a kernel
    // event: hand the waiter over, let the multiplexer dispatch it.
    static notification::NotifierChannel& notifier_channel();

    // The standing channels owned by the engine.
    static time::TimerChannel& timer_channel();

    // Lazily created: constructing the Signal channel intercepts SIGINT and
    // SIGTERM, which must only happen if the application asks for it.
    static signal::SystemSignalChannel& signal_channel();

   private:
    explicit Engine(std::unique_ptr<Multiplexer> multiplexer);

    static std::function<void(bool)> make_idle_hook(Multiplexer& multiplexer);

    std::unique_ptr<Multiplexer> multiplexer_;
    nbio::async::Scheduler scheduler_;
    nbio::core::Timer timer_;
    time::TimerChannel timer_channel_;
    nbio::notification::Notifier notifier_;
    notification::NotifierChannel notifier_channel_;
    std::unique_ptr<nbio::core::SystemSignal> signal_;
    std::unique_ptr<signal::SystemSignalChannel> signal_channel_;

    static thread_local std::unique_ptr<Engine> engine_;
};

// Creates the platform default backend
std::unique_ptr<Multiplexer> make_default_multiplexer();
}  // namespace nbio::runtime::Daemon