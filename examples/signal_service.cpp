#include <nbio/async/scheduler.hpp>
#include <nbio/signal/system_signal.hpp>
#include <nbio/core/epoll_multiplexer.hpp>
#include <nbio/signal/system_signal_channel.hpp>
#include <iostream>
#include <thread>

using namespace nbio;

// SystemSignalChannel intercepts Signals.
// The user is responsible to deal with Signals.
// This example shows how async::wait_for_Signal works.

int main(int argc, char* argv[]) {
    {
        nbio::core::EpollMultiplexer mux;
        async::Scheduler sched([](bool) {});
        // creating SystemSignalChannel will intercept Signals
        signal::SystemSignal Signal;
        nbio::signal::SystemSignalChannel svc(Signal, mux, sched);
        // the process won't exit within 2 seconds
        std::this_thread::sleep_for(std::chrono::seconds(2));
        std::cout << "2s elapsed" << std::endl;
    }
    std::cout << "The process is exitting normally" << std::endl;
    return 0;
}


