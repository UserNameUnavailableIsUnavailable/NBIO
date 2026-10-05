#include <NBIO/FS/FileStreamService.hpp>

#include <NBIO/Async/Runtime.hpp>
#include <span>
#include <utility>

namespace NBIO::FS {
FileStreamService::FileStreamService(const std::filesystem::path& path, NBIO::FS::FileMode mode,
                                     std::filesystem::perms permissions)
    :  // `FileStream` is what the two channels are built on, and it is the engine of this
       // thread that will watch them.
    stream_(path.string(), mode, permissions, NBIO::Async::Runtime::multiplexer(), NBIO::Async::Runtime::scheduler()) {}

NBIO::Async::Task<Utility::expected<std::size_t, std::error_code>> FileStreamService::Read(std::span<char> buffer) {
    return stream_.read(buffer);
}

NBIO::Async::Task<Utility::expected<std::size_t, std::error_code>> FileStreamService::Write(
    std::span<const char> buffer) {
    return stream_.write(buffer);
}
}  // namespace NBIO::FS




