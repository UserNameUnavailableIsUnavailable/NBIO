#pragma once

#include <nbio/async/scheduler.hpp>
#include <nbio/signal/system_signal.hpp>
#include <nbio/time/system_timer.hpp>
#include <nbio/core/multiplexer.hpp>
#include <functional>
#include <memory>

#include <nbio/notification/event_notify_channel.hpp>
#include <nbio/notification/event_notifier.hpp>
#include <nbio/signal/system_signal_channel.hpp>
#include <nbio/time/system_timer_channel.hpp>

namespace nbio::async {
class Runtime {
    struct Init {
        std::unique_ptr<core::Multiplexer> multiplexer;
    };
   public:
    explicit Runtime(Init init);
    Runtime(const Runtime&) = delete;
    Runtime& operator=(const Runtime&) = delete;
    Runtime(Runtime&&) = delete;
    Runtime& operator=(Runtime&&) = delete;

    // Installs a backend on the current thread. Throws if one is already
    // installed in the current thread.
    static void Initialize(std::unique_ptr<core::Multiplexer> multiplexer);

    ~Runtime() noexcept;

    static bool is_initialized() { return static_cast<bool>(runtime_); }

    // The engine current on this thread. Throws when installed() is false.
    static Runtime& instance();

    // ---- what nbio::async asks of a Runtime tag ----
    static nbio::async::Scheduler& scheduler();
    static core::Multiplexer& multiplexer();

    // The channel carrying application-generated events. ConditionVariable
    // binds to it, so an application event travels the same path as a kernel
    // event: hand the waiter over, let the multiplexer dispatch it.
    static notification::EventNotifyChannel& notify_channel();

    // The standing channels owned by the engine.
    static time::SystemTimerChannel& timer_channel();

    // Lazily created: constructing the Signal channel intercepts SIGINT and
    // SIGTERM, which must only happen if the application asks for it.
    static signal::SystemSignalChannel& signal_channel();

   private:

    static std::function<void(bool)> make_idle_hook(core::Multiplexer& multiplexer);

    std::unique_ptr<core::Multiplexer> multiplexer_;
    nbio::async::Scheduler scheduler_;
    nbio::time::SystemTimer timer_;
    time::SystemTimerChannel timer_channel_;
    nbio::notification::EventNotifier notifier_;
    notification::EventNotifyChannel notify_channel_;
    std::unique_ptr<nbio::signal::SystemSignal> signal_;
    std::unique_ptr<signal::SystemSignalChannel> signal_channel_;

    static thread_local std::unique_ptr<Runtime> runtime_;
};

// Creates the platform default backend
std::unique_ptr<core::Multiplexer> make_default_multiplexer();
}  // namespace nbio::async