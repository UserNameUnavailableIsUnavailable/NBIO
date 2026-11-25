#include <filesystem>
#include <nbio/fs/file_read_channel.hpp>
#include <nbio/fs/file_stream.hpp>
#include <nbio/fs/file_write_channel.hpp>
#include <nbio/runtime/daemon.hpp>
#include <stdexcept>

namespace nbio::fs {
namespace {
FileMode ExplicitMode(FileMode mode) {
    if ((mode & FileMode::kAppend) == FileMode::kAppend) {
        throw std::invalid_argument("FileStream requires explicit offsets and cannot use append mode");
    }
    return mode;
}
}  // namespace

FileStream::FileStream(const std::string& path, nbio::fs::FileMode mode, std::filesystem::perms permissions,
                       nbio::core::Multiplexer& multiplexer, nbio::async::Scheduler& scheduler)
    : file_(path, ExplicitMode(mode), permissions),
      read_channel_(std::make_unique<FileReadChannel>(*this, multiplexer, scheduler)),
      write_channel_(std::make_unique<FileWriteChannel>(*this, multiplexer, scheduler)) {
    const auto size = file_.Size();
    if (!size) throw std::system_error(size.error(), "querying file size");
    expected_size_ = *size;
}

nbio::async::Task<utility::expected<std::size_t, std::error_code>> FileStream::Read(std::span<char> buffer,
                                                                                 std::uint64_t offset) {
    return read_channel().Read(buffer, offset);
}

nbio::async::Task<utility::expected<std::size_t, std::error_code>> FileStream::Write(std::span<const char> buffer,
                                                                                  std::uint64_t offset) {
    return write_channel().Write(buffer, offset);
}

utility::expected<std::uint64_t, std::error_code> FileStream::GetActualSize() const { return file_.Size(); }

utility::expected<std::uint64_t, std::error_code> FileStream::GetExpectedSize() const {
    if (error_) return utility::unexpected<std::error_code>(error_);
    return expected_size_;
}

FileReadChannel& FileStream::read_channel() noexcept { return *read_channel_; }

FileWriteChannel& FileStream::write_channel() noexcept { return *write_channel_; }
}  // namespace nbio::fs
