#pragma once

#include <cstdint>
#include <nbio/utility/expected.hpp>
#include <system_error>

namespace nbio::notification {
class Notifier {
   public:
    Notifier();
    ~Notifier() noexcept;

    Notifier(const Notifier&) = delete;
    Notifier& operator=(const Notifier&) = delete;
    Notifier(Notifier&&) = delete;
    Notifier& operator=(Notifier&&) = delete;

    std::uintptr_t native_handle() const noexcept { return handle_; }
    void NonBlocking(bool enabled = true);
    void Notify();
    utility::expected<void, std::error_code> Wait();

   private:
    std::uintptr_t handle_;
};
}  // namespace nbio::notification
