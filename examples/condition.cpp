#include <nbio/async.hpp>
#include <nbio/async/Task.hpp>
#include <nbio/notification/ConditionVariable.hpp>
#include <nbio/async/Runtime.hpp>
#include <nbio/core/URingMultiplexer.hpp>
#include <atomic>
#include <chrono>
#include <future>
#include <iostream>
#include <thread>

std::atomic_bool ok{false};

using namespace nbio;

nbio::async::Task<void> wait_condition(nbio::notification::ConditionVariable& cv) {
    co_await cv.wait([] { return ok.load(std::memory_order_acquire); });
    std::cout << "condition satisfied" << std::endl;
}

int main() {
    auto multiplexer = std::make_unique<nbio::Core::URingMultiplexer>();
    nbio::async::Runtime::initialize(std::move(multiplexer));
    nbio::notification::ConditionVariable condition;
    std::future<void> task = std::async([&condition] {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        ok.store(true, std::memory_order_release);
        std::cout << "sleep finished" << std::endl;
        condition.NotifyOne();
    });
    nbio::async::Run(wait_condition(condition));
}




