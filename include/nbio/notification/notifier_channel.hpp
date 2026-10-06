#pragma once

#include <cstddef>
#include <mutex>
#include <nbio/async/coroutine.hpp>
#include <nbio/async/scheduler.hpp>
#include <nbio/async/task.hpp>
#include <nbio/core/channel.hpp>
#include <nbio/core/types.hpp>
#include <nbio/notification/notifier.hpp>
#include <vector>

namespace nbio::notification {
namespace detail {
template <typename C>
class PollPayload {
   public:
    bool WantsPoll() const noexcept { return !submitted_; }
    void TakePoll() noexcept { submitted_ = true; }
    void ReleasePoll() noexcept { submitted_ = false; }
    bool outstanding() const noexcept { return submitted_; }

   private:
    bool submitted_{false};
};
}  // namespace detail

class NotifierChannel;

class NotifierChannel final : public nbio::core::Channel<NotifierChannel> {
   public:
    using Payload = detail::PollPayload<NotifierChannel>;

    NotifierChannel(nbio::notification::Notifier& notifier, nbio::core::Multiplexer& multiplexer,
                       nbio::async::Scheduler& scheduler);
    ~NotifierChannel() noexcept;

    // The operation this channel wants from the backend is a one-shot poll; the
    // payload carries only whether one is already out there.
    Payload& Submit();
    void Complete();

    // A waiter is about to sleep on the condition variable this channel serves.
    // From here until it is resumed a notification has to be seen, so a read is
    // prepared and the channel armed. This runs on the thread the waiting
    // coroutine runs on -- this channel's own engine -- which is what makes arming
    // safe here; the handover below happens on whatever thread notifies, and only
    // touches the queue and the eventfd.
    void waiter_registered();

    template <typename It>
    void Park(It begin, It end);

    void Park(nbio::async::Coroutine notifiee);

    nbio::notification::Notifier& notifier() noexcept { return notifier_; }

    const nbio::notification::Notifier& notifier() const noexcept { return notifier_; }

   private:
    nbio::notification::Notifier& notifier_;
    std::mutex mutex_;
    std::vector<nbio::async::Coroutine> notifiees_;
    std::size_t Parked_{0};
    Payload payload_{};
};

template <typename It>
inline void NotifierChannel::Park(It begin, It end) {
    std::lock_guard lock(mutex_);
    notifiees_.insert(notifiees_.end(), begin, end);
    notifier_.Notify();
}
}  // namespace nbio::notification
