#include <iostream>
#include <nbio/async.hpp>
#include <nbio/runtime/daemon.hpp>
#include <nbio/signal/system_signal_service.hpp>

using namespace nbio::async;

Task<void> Grace() {
    co_await nbio::signal::SystemSignalService{}.wait();
    co_await nbio::time::SystemTimeService{}.Sleep(std::chrono::seconds(2));
    std::cout << "Period of grace ends" << std::endl;
    exit(1);
}

int main(int argc, char* argv[]) {
    Spawn(Grace());
    Run([]() -> Task<void> { co_await nbio::time::SystemTimeService{}.Sleep(std::chrono::seconds(10)); }());
    std::cout << "The process exited gracefully" << std::endl;
    return 0;
}
