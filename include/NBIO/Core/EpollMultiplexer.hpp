#pragma once
#if defined(__linux__)

#include <sys/epoll.h>

#include <NBIO/Async/Scheduler.hpp>
#include <NBIO/Async/Task.hpp>
#include <NBIO/Core/Channel.hpp>
#include <NBIO/Core/Types.hpp>
#include <chrono>
#include <unordered_map>
#include <vector>

namespace NBIO::Core {
class EpollMultiplexer final : public NBIO::Core::Multiplexer {
   public:
    using Handle = int;
    EpollMultiplexer();
    virtual ~EpollMultiplexer() noexcept;
    virtual void Run() override;
    virtual void RunFor(std::chrono::milliseconds timeout) override;
    virtual void AddChannel(NBIO::Core::ChannelBase* channel) override;
    virtual void DeleteChannel(NBIO::Core::ChannelBase* channel) noexcept override;

   private:
    void RunImpl(int timeout);
    int handle_{-1};                                                                  // epoll file descriptor
    std::unordered_multimap<int, NBIO::Core::ChannelBase*> pollable_channels_;  // all pollable channels
    std::unordered_multimap<int, NBIO::Core::ChannelBase*>
        always_channels_;  // channels that are always ready, non-pollable
    std::vector<ChannelBase*> active_channels_;
};
}  // namespace NBIO::Core
#endif  // defined(__linux__)


