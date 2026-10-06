#include <chrono>
#include <iostream>
#include <nbio/async.hpp>
#include <nbio/runtime/daemon.hpp>
#include <nbio/time/timer_service.hpp>

using namespace nbio;

nbio::async::Task<void> Sleep() {
    std::cout << "Sleep ";
    std::chrono::steady_clock::time_point before = std::chrono::steady_clock::now();
    co_await nbio::time::SystemTimeService{}.Sleep(std::chrono::seconds(1));
    std::chrono::steady_clock::time_point after = std::chrono::steady_clock::now();
    std::cout << "slept for: " << std::chrono::duration_cast<std::chrono::milliseconds>((after - before)).count()
              << "ms" << std::endl;
}

nbio::async::Task<void> AllSleep() {
    std::cout << "AllSleep ";
    std::chrono::steady_clock::time_point before = std::chrono::steady_clock::now();
    auto sleep1 = nbio::time::SystemTimeService{}.Sleep(std::chrono::seconds(1));
    auto sleep2 = nbio::time::SystemTimeService{}.Sleep(std::chrono::seconds(2));
    auto sleep3 = nbio::time::SystemTimeService{}.Sleep(std::chrono::seconds(3));
    co_await async::WhenAll(std::move(sleep1), std::move(sleep2), std::move(sleep3));
    std::chrono::steady_clock::time_point after = std::chrono::steady_clock::now();
    std::cout << "slept for: " << std::chrono::duration_cast<std::chrono::milliseconds>((after - before)).count()
              << "ms" << std::endl;
}

nbio::async::Task<void> AnySleep() {
    std::cout << "AnySleep ";
    std::chrono::steady_clock::time_point before = std::chrono::steady_clock::now();
    auto sleep1 = nbio::time::SystemTimeService{}.Sleep(std::chrono::seconds(1));
    auto sleep2 = nbio::time::SystemTimeService{}.Sleep(std::chrono::seconds(2));
    auto sleep3 = nbio::time::SystemTimeService{}.Sleep(std::chrono::seconds(3));
    co_await async::WhenAny(std::move(sleep1), std::move(sleep2), std::move(sleep3));
    std::chrono::steady_clock::time_point after = std::chrono::steady_clock::now();
    std::cout << "slept for: " << std::chrono::duration_cast<std::chrono::milliseconds>((after - before)).count()
              << "ms" << std::endl;
}

int main() {
    nbio::async::Run([]() -> nbio::async::Task<void> {
        co_await Sleep();
        co_await AllSleep();
        co_await AnySleep();
    }());
}
