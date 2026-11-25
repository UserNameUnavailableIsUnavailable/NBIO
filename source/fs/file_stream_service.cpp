#include <nbio/fs/file_stream_service.hpp>
#include <nbio/runtime/daemon.hpp>
#include <span>
#include <utility>

namespace nbio::fs {
FileStreamService::FileStreamService(const std::filesystem::path& path, nbio::fs::FileMode mode,
                                     std::filesystem::perms permissions)
    :  // `FileStream` is what the two channels are built on, and it is the engine of this
       // thread that will watch them.
      stream_(path.string(), mode, permissions, nbio::runtime::Daemon::multiplexer(),
              nbio::runtime::Daemon::scheduler()) {}

nbio::async::Task<utility::expected<std::size_t, std::error_code>> FileStreamService::Read(std::span<char> buffer,
                                                                                        std::uint64_t offset) {
    return stream_.Read(buffer, offset);
}

nbio::async::Task<utility::expected<std::size_t, std::error_code>> FileStreamService::Write(
    std::span<const char> buffer, std::uint64_t offset) {
    return stream_.Write(buffer, offset);
}
}  // namespace nbio::fs
