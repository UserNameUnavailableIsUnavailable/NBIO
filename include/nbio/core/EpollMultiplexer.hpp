#pragma once
#if defined(__linux__)

#include <sys/epoll.h>

#include <nbio/async/Scheduler.hpp>
#include <nbio/async/Task.hpp>
#include <nbio/core/Channel.hpp>
#include <nbio/core/Types.hpp>
#include <chrono>
#include <unordered_map>
#include <vector>

namespace nbio::Core {
class EpollMultiplexer final : public nbio::Core::Multiplexer {
   public:
    using Handle = int;
    EpollMultiplexer();
    virtual ~EpollMultiplexer() noexcept;
    virtual void Run() override;
    virtual void RunFor(std::chrono::milliseconds timeout) override;
    virtual void AddChannel(nbio::Core::ChannelBase* channel) override;
    virtual void DeleteChannel(nbio::Core::ChannelBase* channel) noexcept override;

   private:
    void RunImpl(int timeout);
    int handle_{-1};                                                                  // epoll file descriptor
    std::unordered_multimap<int, nbio::Core::ChannelBase*> pollable_channels_;  // all pollable channels
    std::unordered_multimap<int, nbio::Core::ChannelBase*>
        always_channels_;  // channels that are always ready, non-pollable
    std::vector<ChannelBase*> active_channels_;
};
}  // namespace nbio::Core
#endif  // defined(__linux__)


