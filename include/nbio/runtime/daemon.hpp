#pragma once

#include <functional>
#include <memory>
#include <nbio/async/scheduler.hpp>
#include <nbio/core/multiplexer.hpp>
#include <nbio/notification/notifier.hpp>
#include <nbio/notification/notifier_channel.hpp>
#include <nbio/signal/system_signal.hpp>
#include <nbio/signal/system_signal_channel.hpp>
#include <nbio/time/timer.hpp>
#include <nbio/time/timer_channel.hpp>

namespace nbio::runtime {
class Daemon {
    struct Init {
        std::unique_ptr<core::Multiplexer> multiplexer;
    };

   public:
    explicit Daemon(Init init);
    Daemon(const Daemon&) = delete;
    Daemon& operator=(const Daemon&) = delete;
    Daemon(Daemon&&) = delete;
    Daemon& operator=(Daemon&&) = delete;

    static void Initialize(std::unique_ptr<core::Multiplexer> multiplexer);

    ~Daemon() noexcept;

    static bool is_initialized() { return static_cast<bool>(daemon_); }

    static Daemon& instance();

    static nbio::async::Scheduler& scheduler();
    static nbio::core::Multiplexer& multiplexer();

    static nbio::notification::NotifierChannel& notifier_channel();
    static nbio::time::TimerChannel& timer_channel();
    static nbio::signal::SystemSignalChannel& signal_channel();

   private:
    static thread_local std::unique_ptr<Daemon> daemon_;

    static std::function<void(bool)> make_idle_hook(core::Multiplexer& multiplexer);

    std::unique_ptr<nbio::core::Multiplexer> multiplexer_;
    std::unique_ptr<nbio::async::Scheduler> scheduler_;

	std::unique_ptr<nbio::time::Timer> timer_;
    std::unique_ptr<nbio::time::TimerChannel> timer_channel_;
    std::unique_ptr<nbio::notification::Notifier> notifier_;
    std::unique_ptr<nbio::notification::NotifierChannel> notifier_channel_;
    std::unique_ptr<nbio::signal::SystemSignal> signal_;
    std::unique_ptr<nbio::signal::SystemSignalChannel> signal_channel_;
};

// Creates the platform default backend
std::unique_ptr<nbio::core::Multiplexer> make_default_multiplexer();
}  // namespace nbio::runtime
