#include <NBIO/Async.hpp>
#include <NBIO/Async/Runtime.hpp>
#include <NBIO/Time/SystemTimeService.hpp>
#include <chrono>
#include <iostream>

using namespace NBIO;

NBIO::Async::Task<void> Sleep() {
    std::cout << "Sleep ";
    std::chrono::steady_clock::time_point before = std::chrono::steady_clock::now();
    co_await NBIO::Time::SystemTimeService{}.sleep(std::chrono::seconds(1));
    std::chrono::steady_clock::time_point after = std::chrono::steady_clock::now();
    std::cout << "slept for: " << std::chrono::duration_cast<std::chrono::milliseconds>((after - before)).count()
              << "ms" << std::endl;
}

NBIO::Async::Task<void> AllSleep() {
    std::cout << "AllSleep ";
    std::chrono::steady_clock::time_point before = std::chrono::steady_clock::now();
    auto sleep1 = NBIO::Time::SystemTimeService{}.sleep(std::chrono::seconds(1));
    auto sleep2 = NBIO::Time::SystemTimeService{}.sleep(std::chrono::seconds(2));
    auto sleep3 = NBIO::Time::SystemTimeService{}.sleep(std::chrono::seconds(3));
    co_await Async::WhenAll(std::move(sleep1), std::move(sleep2), std::move(sleep3));
    std::chrono::steady_clock::time_point after = std::chrono::steady_clock::now();
    std::cout << "slept for: " << std::chrono::duration_cast<std::chrono::milliseconds>((after - before)).count()
              << "ms" << std::endl;
}

NBIO::Async::Task<void> AnySleep() {
    std::cout << "AnySleep ";
    std::chrono::steady_clock::time_point before = std::chrono::steady_clock::now();
    auto sleep1 = NBIO::Time::SystemTimeService{}.sleep(std::chrono::seconds(1));
    auto sleep2 = NBIO::Time::SystemTimeService{}.sleep(std::chrono::seconds(2));
    auto sleep3 = NBIO::Time::SystemTimeService{}.sleep(std::chrono::seconds(3));
    co_await Async::WhenAny(std::move(sleep1), std::move(sleep2), std::move(sleep3));
    std::chrono::steady_clock::time_point after = std::chrono::steady_clock::now();
    std::cout << "slept for: " << std::chrono::duration_cast<std::chrono::milliseconds>((after - before)).count()
              << "ms" << std::endl;
}

int main() {
    NBIO::Async::Run([]() -> NBIO::Async::Task<void> {
        co_await Sleep();
        co_await AllSleep();
        co_await AnySleep();
    }());
}


