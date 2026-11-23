#pragma once
#if defined(_WIN32) || defined(_WIN64)

#include <memory>
#include <nbio/core/multiplexer.hpp>

namespace nbio::core {
class IOCPMultiplexer : public Multiplexer {
public:
    IOCPMultiplexer();
    IOCPMultiplexer(const IOCPMultiplexer&) = delete;
    IOCPMultiplexer& operator=(const IOCPMultiplexer&) = delete;
    virtual ~IOCPMultiplexer() noexcept;
    virtual void Wait() override;
    virtual void WaitFor(std::chrono::milliseconds timeout) override;
    virtual void AddChannel(nbio::core::ChannelBase* channel) override;
    virtual void DeleteChannel(nbio::core::ChannelBase* channel) noexcept override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}  // namespace nbio::core

#endif  // defined(_WIN32) || defined(_WIN64)