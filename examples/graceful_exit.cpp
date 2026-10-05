#include <nbio/async.hpp>
#include <nbio/async/Runtime.hpp>
#include <nbio/signal/SystemSignalService.hpp>
#include <iostream>

using namespace nbio::async;

Task<void> Grace() {
    co_await nbio::signal::SystemSignalService{}.wait();
    co_await nbio::time::SystemTimeService{}.sleep(std::chrono::seconds(2));
    std::cout << "Period of grace ends" << std::endl;
    exit(1);
}

int main(int argc, char* argv[]) {
    Spawn(Grace());
    Run([]() -> Task<void> { co_await nbio::time::SystemTimeService{}.sleep(std::chrono::seconds(10)); }());
    std::cout << "The process exited gracefully" << std::endl;
    return 0;
}


