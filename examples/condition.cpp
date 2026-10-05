#include <nbio/async.hpp>
#include <nbio/async/task.hpp>
#include <nbio/notification/condition_variable.hpp>
#include <nbio/async/runtime.hpp>
#include <nbio/core/uring_multiplexer.hpp>
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
    auto multiplexer = std::make_unique<nbio::core::URingMultiplexer>();
    nbio::async::Runtime::Initialize(std::move(multiplexer));
    nbio::notification::ConditionVariable condition;
    std::future<void> task = std::async([&condition] {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        ok.store(true, std::memory_order_release);
        std::cout << "sleep finished" << std::endl;
        condition.NotifyOne();
    });
    nbio::async::Run(wait_condition(condition));
}




