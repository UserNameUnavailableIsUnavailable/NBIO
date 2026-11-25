#pragma once

#include <cstdint>
#include <deque>
#include <filesystem>
#include <memory>
#include <nbio/async/scheduler.hpp>
#include <nbio/async/task.hpp>
#include <nbio/core/channel.hpp>
#include <nbio/core/types.hpp>
#include <nbio/fs/file.hpp>
#include <string>

#include "file_read_channel.hpp"
#include "file_write_channel.hpp"

namespace nbio::core {
class IOCPMultiplexer;
}

namespace nbio::fs {
class FileReadChannel;
class FileWriteChannel;

// One file, and the two channels that read and write it.
//
// Strictly owned: made in place where it lives, never moved and never shared. It has
// to stay put because the multiplexer holds a pointer to each channel, and because
// each channel holds a reference to this -- and it is shared with nobody, because
// the channels are its only referrers and it is the one thing they refer to.
class FileStream {
public:
    FileStream(const std::string& path, nbio::fs::FileMode mode, std::filesystem::perms permissions,
               nbio::core::Multiplexer& multiplexer, nbio::async::Scheduler& scheduler);
    FileStream(const FileStream&) = delete;
    FileStream& operator=(const FileStream&) = delete;
    FileStream(FileStream&&) = delete;
    FileStream& operator=(FileStream&&) = delete;
    // The file is closed by `file_`'s own destructor, which runs *after* the
    // channels are destroyed. Closing it here, in the body, would release the
    // descriptor while a channel still names it -- and the number could be handed
    // to another open before that channel unregisters itself.
    ~FileStream() noexcept = default;

    nbio::async::Task<utility::expected<std::size_t, std::error_code>> Read(std::span<char> buffer,
                                                                           std::uint64_t offset);
    nbio::async::Task<utility::expected<std::size_t, std::error_code>> Write(std::span<const char> buffer,
                                                                            std::uint64_t offset);

    std::uintptr_t native_handle() const noexcept { return file_.native_handle(); }

    utility::expected<std::uint64_t, std::error_code> GetActualSize() const;
    utility::expected<std::uint64_t, std::error_code> GetExpectedSize() const;

    FileReadChannel& read_channel() noexcept;
    FileWriteChannel& write_channel() noexcept;

private:
    friend class FileReadChannel;
    friend class FileWriteChannel;
    enum class Direction { kRead, kWrite };
    struct Group {
        Direction direction;
        std::size_t size;
    };

    void Enqueue(Direction direction);
    bool CanSubmit(Direction direction) const noexcept;
    std::size_t SubmissionLimit(Direction direction) const noexcept;
    void Complete(Direction direction);

    void ObserveWrite(std::uint64_t end) noexcept {
        if (end > expected_size_) expected_size_ = end;
    }
    friend class core::IOCPMultiplexer;
#if defined(_WIN32)
    std::uintptr_t completion_port_{};
#endif
    nbio::fs::File file_;
    std::unique_ptr<FileReadChannel> read_channel_;
    std::unique_ptr<FileWriteChannel> write_channel_;
    std::uint64_t expected_size_{0};
    std::deque<Group> queue_;
    std::error_code error_;
};

}  // namespace nbio::fs
