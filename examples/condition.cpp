#include <NBIO/Async.hpp>
#include <NBIO/Async/Task.hpp>
#include <NBIO/Notification/ConditionVariable.hpp>
#include <NBIO/Async/Runtime.hpp>
#include <NBIO/Core/URingMultiplexer.hpp>
#include <atomic>
#include <chrono>
#include <future>
#include <iostream>
#include <thread>

std::atomic_bool ok{false};

using namespace NBIO;

NBIO::Async::Task<void> wait_condition(NBIO::Notification::ConditionVariable& cv) {
    co_await cv.wait([] { return ok.load(std::memory_order_acquire); });
    std::cout << "condition satisfied" << std::endl;
}

int main() {
    auto multiplexer = std::make_unique<NBIO::Core::URingMultiplexer>();
    NBIO::Async::Runtime::initialize(std::move(multiplexer));
    NBIO::Notification::ConditionVariable condition;
    std::future<void> task = std::async([&condition] {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        ok.store(true, std::memory_order_release);
        std::cout << "sleep finished" << std::endl;
        condition.NotifyOne();
    });
    NBIO::Async::Run(wait_condition(condition));
}




