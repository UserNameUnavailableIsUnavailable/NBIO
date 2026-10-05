#pragma once

#include <NBIO/Async/Scheduler.hpp>
#include <NBIO/Signal/SystemSignal.hpp>
#include <NBIO/Time/SystemTimer.hpp>
#include <NBIO/Core/Multiplexer.hpp>
#include <functional>
#include <memory>

#include <NBIO/Notification/EventNotifyChannel.hpp>
#include <NBIO/Notification/EventNotifier.hpp>
#include <NBIO/Signal/SystemSignalChannel.hpp>
#include <NBIO/Time/SystemTimerChannel.hpp>

namespace NBIO::Async {
class Runtime {
    struct Init {
        std::unique_ptr<Core::Multiplexer> multiplexer;
    };
   public:
    explicit Runtime(Init init);
    Runtime(const Runtime&) = delete;
    Runtime& operator=(const Runtime&) = delete;
    Runtime(Runtime&&) = delete;
    Runtime& operator=(Runtime&&) = delete;

    // Installs a backend on the current thread. Throws if one is already
    // installed in the current thread.
    static void initialize(std::unique_ptr<Core::Multiplexer> multiplexer);

    ~Runtime() noexcept;

    static bool is_initialized() { return static_cast<bool>(runtime_); }

    // The engine current on this thread. Throws when installed() is false.
    static Runtime& instance();

    // ---- what NBIO::Async asks of a Runtime tag ----
    static NBIO::Async::Scheduler& scheduler();
    static Core::Multiplexer& multiplexer();

    // The channel carrying application-generated events. ConditionVariable
    // binds to it, so an application event travels the same path as a kernel
    // event: hand the waiter over, let the multiplexer dispatch it.
    static Notification::EventNotifyChannel& notify_channel();

    // The standing channels owned by the engine.
    static Time::SystemTimerChannel& timer_channel();

    // Lazily created: constructing the signal channel intercepts SIGINT and
    // SIGTERM, which must only happen if the application asks for it.
    static Signal::SystemSignalChannel& signal_channel();

   private:

    static std::function<void(bool)> make_idle_hook(Core::Multiplexer& multiplexer);

    std::unique_ptr<Core::Multiplexer> multiplexer_;
    NBIO::Async::Scheduler scheduler_;
    NBIO::Time::SystemTimer timer_;
    Time::SystemTimerChannel timer_channel_;
    NBIO::Notification::EventNotifier notifier_;
    Notification::EventNotifyChannel notify_channel_;
    std::unique_ptr<NBIO::Signal::SystemSignal> signal_;
    std::unique_ptr<Signal::SystemSignalChannel> signal_channel_;

    static thread_local std::unique_ptr<Runtime> runtime_;
};

// Creates the platform default backend
std::unique_ptr<Core::Multiplexer> make_default_multiplexer();
}  // namespace NBIO::Async