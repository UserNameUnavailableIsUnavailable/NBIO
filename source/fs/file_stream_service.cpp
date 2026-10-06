#include <nbio/runtime/daemon.hpp>
#include <nbio/fs/file_stream_service.hpp>
#include <span>
#include <utility>

namespace nbio::fs {
FileStreamService::FileStreamService(const std::filesystem::path& path, nbio::fs::FileMode mode,
                                     std::filesystem::perms permissions)
    :  // `FileStream` is what the two channels are built on, and it is the engine of this
       // thread that will watch them.
      stream_(path.string(), mode, permissions, nbio::runtime::Daemon::multiplexer(),
              nbio::runtime::Daemon::scheduler()) {}

nbio::async::Task<utility::expected<std::size_t, std::error_code>> FileStreamService::Read(std::span<char> buffer) {
    return stream_.read(buffer);
}

nbio::async::Task<utility::expected<std::size_t, std::error_code>> FileStreamService::Write(
    std::span<const char> buffer) {
    return stream_.write(buffer);
}
}  // namespace nbio::fs
