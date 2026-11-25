#include <gtest/gtest.h>
#include <unistd.h>

#include <array>
#include <exception>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <nbio/async.hpp>
#include <nbio/core/epoll_multiplexer.hpp>
#include <nbio/fs/file_stream_service.hpp>
#include <string>
#include <thread>
#include <vector>
#if defined(NBIO_ENABLE_IO_URING)
#include <nbio/core/io_uring_multiplexer.hpp>
#endif

namespace {
using File = nbio::fs::FileStreamService;

nbio::async::Task<void> Write(File& file, std::string_view bytes, std::uint64_t offset) {
    const auto result = co_await file.Write(std::span<const char>(bytes.data(), bytes.size()), offset);
    if (!result) throw std::system_error(result.error(), "test write");
    EXPECT_EQ(*result, bytes.size());
}

nbio::async::Task<void> Read(File& file, std::span<char> bytes, std::uint64_t offset) {
    const auto result = co_await file.Read(bytes, offset);
    if (!result) throw std::system_error(result.error(), "test read");
    EXPECT_EQ(*result, bytes.size());
}

nbio::async::Task<void> OffsetsAndSizes(const std::filesystem::path& path) {
    File file(path);
    EXPECT_EQ(file.GetActualSize().value(), 4);
    EXPECT_EQ(file.GetExpectedSize().value(), 4);
    const auto handle = static_cast<int>(file.stream().native_handle());
    EXPECT_EQ(::lseek(handle, 1, SEEK_SET), 1);
    co_await Write(file, "XY", 1);
    std::array<char, 4> bytes{};
    co_await Read(file, bytes, 0);
    EXPECT_EQ(std::string(bytes.data(), bytes.size()), "aXYd");
    co_await Write(file, "z", 8);
    EXPECT_EQ(file.GetActualSize().value(), 9);
    EXPECT_EQ(file.GetExpectedSize().value(), 9);
    EXPECT_EQ(::lseek(handle, 0, SEEK_CUR), 1);

    auto eof = co_await file.Read(bytes, 50);
    EXPECT_TRUE(eof);
    if (eof) EXPECT_EQ(*eof, 0);
    auto empty = co_await file.Write({}, 100);
    EXPECT_TRUE(empty);
    if (empty) EXPECT_EQ(*empty, 0);
    EXPECT_EQ(file.GetActualSize().value(), 9);
    EXPECT_EQ(file.GetExpectedSize().value(), 9);
    {
        auto unstarted = file.Write(std::span<const char>("x", 1), 200);
        EXPECT_EQ(file.GetExpectedSize().value(), 9);
    }

    const auto maximum = static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
    const auto invalid_read = co_await file.Read(bytes, maximum + 1);
    EXPECT_FALSE(invalid_read);
    if (!invalid_read) EXPECT_EQ(invalid_read.error(), std::make_error_code(std::errc::invalid_argument));
    const auto invalid_write = co_await file.Write(std::span<const char>("x", 1), maximum);
    EXPECT_FALSE(invalid_write);
    if (!invalid_write) EXPECT_EQ(invalid_write.error(), std::make_error_code(std::errc::invalid_argument));
    co_await Write(file, "X", 1);
    EXPECT_EQ(file.GetExpectedSize().value(), 9);
}

nbio::async::Task<void> ObserveQueued(File& file) {
    EXPECT_FALSE(file.stream().write_channel().is_idle());
    EXPECT_EQ(file.GetActualSize().value(), 0);
    EXPECT_EQ(file.GetExpectedSize().value(), 8);
    co_return;
}

nbio::async::Task<void> QueuedOffsets(const std::filesystem::path& path) {
    File file(path);
    co_await nbio::async::WhenAll(Write(file, "abcd", 0), Write(file, "efgh", 4), Write(file, "XY", 0),
                                  ObserveQueued(file));
    EXPECT_EQ(file.GetActualSize().value(), 8);
    EXPECT_EQ(file.GetExpectedSize().value(), 8);
    std::array<char, 2> first{}, second{}, third{};
    co_await nbio::async::WhenAll(Read(file, first, 6), Read(file, second, 0), Read(file, third, 2));
    EXPECT_EQ(std::string(first.data(), first.size()), "gh");
    EXPECT_EQ(std::string(second.data(), second.size()), "XY");
    EXPECT_EQ(std::string(third.data(), third.size()), "cd");
}

nbio::async::Task<void> MixedOperations(const std::filesystem::path& path) {
    File file(path);
    std::array<char, 2> first{}, second{};
    std::array<char, 4> last{};
    co_await nbio::async::WhenAll(Write(file, "12", 0), Read(file, first, 0), Write(file, "34", 0),
                                  Read(file, second, 0), Write(file, "56", 2), Read(file, last, 0));
    EXPECT_EQ(std::string(first.data(), first.size()), "12");
    EXPECT_EQ(std::string(second.data(), second.size()), "34");
    EXPECT_EQ(std::string(last.data(), last.size()), "3456");
}

nbio::async::Task<void> CaptureFailure(File& file, std::uint64_t offset, std::error_code& error) {
    const auto result = co_await file.Write(std::span<const char>("x", 1), offset);
    EXPECT_FALSE(result);
    if (!result) error = result.error();
}

nbio::async::Task<void> CaptureReadFailure(File& file, std::error_code& error) {
    std::array<char, 1> bytes{};
    const auto result = co_await file.Read(bytes, 0);
    EXPECT_FALSE(result);
    if (!result) error = result.error();
}

nbio::async::Task<void> WriteFailures(const std::filesystem::path& path) {
    File file(path, nbio::fs::FileMode::kRead);
    std::error_code first, second, read;
    co_await nbio::async::WhenAll(CaptureFailure(file, 0, first), CaptureReadFailure(file, read),
                                  CaptureFailure(file, 10, second));
    EXPECT_TRUE(first);
    EXPECT_EQ(first, second);
    EXPECT_EQ(first, read);
    const auto expected = file.GetExpectedSize();
    EXPECT_FALSE(expected);
    if (!expected) EXPECT_EQ(expected.error(), first);
    EXPECT_EQ(file.GetActualSize().value(), 4);
    std::error_code later;
    co_await CaptureFailure(file, 2, later);
    EXPECT_EQ(later, first);
    co_await CaptureReadFailure(file, later);
    EXPECT_EQ(later, first);
    File reopened(path, nbio::fs::FileMode::kRead);
    std::array<char, 4> bytes{};
    co_await Read(reopened, bytes, 0);
    EXPECT_EQ(std::string(bytes.data(), bytes.size()), "abcd");
}

nbio::async::Task<void> ReadFailures(const std::filesystem::path& path) {
    File file(path, nbio::fs::FileMode::kWrite);
    std::error_code first, second, later;
    co_await nbio::async::WhenAll(CaptureReadFailure(file, first), CaptureFailure(file, 0, second));
    EXPECT_TRUE(first);
    EXPECT_EQ(second, first);
    const auto expected = file.GetExpectedSize();
    EXPECT_FALSE(expected);
    if (!expected) EXPECT_EQ(expected.error(), first);
    co_await CaptureReadFailure(file, later);
    EXPECT_EQ(later, first);
    co_await CaptureFailure(file, 0, later);
    EXPECT_EQ(later, first);
}

enum class Scenario { kOffsets, kQueued, kMixed, kWriteFailure, kReadFailure };

void RunScenario(bool uring, Scenario scenario) {
    std::exception_ptr failure;
    std::thread worker([&] {
        const auto path = std::filesystem::temp_directory_path() /
                          ("nbio-offset-" + std::to_string(::getpid()) + "-" +
                           std::to_string(static_cast<int>(scenario)) + "-" + std::to_string(uring));
        struct Cleanup {
            std::filesystem::path path;
            ~Cleanup() {
                std::error_code error;
                std::filesystem::remove(path, error);
                if (error) ADD_FAILURE() << error.message();
            }
        } cleanup{path};
        try {
            {
                std::ofstream seed(path, std::ios::binary | std::ios::trunc);
                if (!seed) throw std::runtime_error("creating file fixture");
                if (scenario != Scenario::kQueued) seed << "abcd";
            }
#if defined(NBIO_ENABLE_IO_URING)
            if (uring)
                nbio::runtime::Daemon::Initialize(std::make_unique<nbio::core::IOURingMultiplexer>());
            else
#endif
                nbio::runtime::Daemon::Initialize(std::make_unique<nbio::core::EpollMultiplexer>());
            switch (scenario) {
                case Scenario::kOffsets:
                    nbio::async::Run(OffsetsAndSizes(path));
                    break;
                case Scenario::kQueued:
                    nbio::async::Run(QueuedOffsets(path));
                    break;
                case Scenario::kMixed:
                    nbio::async::Run(MixedOperations(path));
                    break;
                case Scenario::kWriteFailure:
                    nbio::async::Run(WriteFailures(path));
                    break;
                case Scenario::kReadFailure:
                    nbio::async::Run(ReadFailures(path));
                    break;
            }
            std::ifstream input(path, std::ios::binary);
            const std::string contents{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
            if (scenario == Scenario::kOffsets) EXPECT_EQ(contents, std::string("aXYd\0\0\0\0z", 9));
            if (scenario == Scenario::kQueued) EXPECT_EQ(contents, "XYcdefgh");
            if (scenario == Scenario::kMixed) EXPECT_EQ(contents, "3456");
            if (scenario == Scenario::kWriteFailure || scenario == Scenario::kReadFailure) EXPECT_EQ(contents, "abcd");
            EXPECT_THROW(File(path, nbio::fs::FileMode::kReadWrite | nbio::fs::FileMode::kAppend),
                         std::invalid_argument);
        } catch (...) {
            failure = std::current_exception();
        }
    });
    worker.join();
    if (failure) std::rethrow_exception(failure);
}

class FileOffsetTest : public testing::TestWithParam<bool> {};
TEST_P(FileOffsetTest, ExplicitOffsetsSizesAndInvalidRanges) { RunScenario(GetParam(), Scenario::kOffsets); }
TEST_P(FileOffsetTest, QueuedNoncontiguousAndOverlappingOperations) { RunScenario(GetParam(), Scenario::kQueued); }
TEST_P(FileOffsetTest, ReadsAndWritesShareSubmissionOrder) { RunScenario(GetParam(), Scenario::kMixed); }
TEST_P(FileOffsetTest, WriteFailurePropagatesToFollowingOperations) { RunScenario(GetParam(), Scenario::kWriteFailure); }
TEST_P(FileOffsetTest, ReadFailurePropagatesToFollowingOperations) { RunScenario(GetParam(), Scenario::kReadFailure); }
#if defined(NBIO_ENABLE_IO_URING)
INSTANTIATE_TEST_SUITE_P(Backends, FileOffsetTest, testing::Values(false, true));
#else
INSTANTIATE_TEST_SUITE_P(Backends, FileOffsetTest, testing::Values(false));
#endif

class HeldMultiplexer : public nbio::core::Multiplexer {
public:
    HeldMultiplexer() : Multiplexer(nbio::core::MultiplexerType::kEpoll) {}
    void AddChannel(nbio::core::ChannelBase*) override {}
    void DeleteChannel(nbio::core::ChannelBase*) noexcept override {}
    void WaitFor(std::chrono::milliseconds) override {}
    void Wait() override {}
};

struct Outcome {
    bool finished = false;
    std::size_t bytes = 0;
    std::error_code error;
};

nbio::async::Task<void> HeldWrite(nbio::fs::FileStream& file, std::string_view bytes, std::uint64_t offset,
                                  Outcome& outcome) {
    const auto result = co_await file.Write(std::span<const char>(bytes.data(), bytes.size()), offset);
    if (result)
        outcome.bytes = *result;
    else
        outcome.error = result.error();
    outcome.finished = true;
}

nbio::async::Task<void> HeldRead(nbio::fs::FileStream& file, std::span<char> bytes, std::uint64_t offset,
                                 Outcome& outcome) {
    const auto result = co_await file.Read(bytes, offset);
    if (result)
        outcome.bytes = *result;
    else
        outcome.error = result.error();
    outcome.finished = true;
}

nbio::async::Task<void> OwnedWrite(nbio::fs::FileStream& file, std::shared_ptr<int> owner) {
    std::array<char, 4> bytes{};
    const auto result = co_await file.Write(bytes, 0);
    EXPECT_TRUE(result);
    EXPECT_EQ(*owner, 7);
}

nbio::async::Task<void> OwnedRead(nbio::fs::FileStream& file, std::shared_ptr<int> owner) {
    std::array<char, 4> bytes{};
    const auto result = co_await file.Read(bytes, 0);
    EXPECT_TRUE(result);
    EXPECT_EQ(*owner, 7);
}

class HeldFileTest : public testing::Test {
protected:
    HeldFileTest()
        : path(std::filesystem::temp_directory_path() / ("nbio-held-offset-" + std::to_string(::getpid()))),
          scheduler([](bool) {}),
          file(path.string(), nbio::fs::FileMode::kReadWrite | nbio::fs::FileMode::kCreate |
                                  nbio::fs::FileMode::kTruncate,
               nbio::fs::File::kDefaultPermissions, multiplexer, scheduler) {}
    ~HeldFileTest() override {
        std::error_code error;
        std::filesystem::remove(path, error);
        EXPECT_FALSE(error) << error.message();
    }
    std::filesystem::path path;
    HeldMultiplexer multiplexer;
    nbio::async::Scheduler scheduler;
    nbio::fs::FileStream file;
};

TEST_F(HeldFileTest, VectorizesContiguousRangesAndPropagatesPartialFailure) {
    Outcome first, second, third;
    scheduler.Spawn(HeldWrite(file, "abcd", 0, first));
    scheduler.Spawn(HeldWrite(file, "efgh", 4, second));
    scheduler.Spawn(HeldWrite(file, "ij", 12, third));
    scheduler.Run();
    EXPECT_EQ(file.GetExpectedSize().value(), 14);
    EXPECT_EQ(file.GetActualSize().value(), 0);
    auto& payload = file.write_channel().Submit();
    EXPECT_EQ(payload.header().size(), 2);
    EXPECT_EQ(payload.offset(), 0);
    payload.CompleteWrite(4);
    file.write_channel().Complete();
    scheduler.Run();
    EXPECT_TRUE(first.finished);
    EXPECT_EQ(first.bytes, 4);
    EXPECT_FALSE(second.finished);
    EXPECT_FALSE(third.finished);
    EXPECT_EQ(payload.header().size(), 1);
    EXPECT_EQ(payload.offset(), 4);
    payload.CompleteWrite(2);
    file.write_channel().Complete();
    EXPECT_EQ(payload.header().size(), 1);
    EXPECT_EQ(payload.offset(), 6);
    const auto failure = std::make_error_code(std::errc::io_error);
    payload.FailSubmitted(failure);
    file.write_channel().Complete();
    scheduler.Run();
    EXPECT_TRUE(second.finished);
    EXPECT_TRUE(third.finished);
    EXPECT_EQ(second.error, failure);
    EXPECT_EQ(third.error, failure);
    const auto expected = file.GetExpectedSize();
    EXPECT_FALSE(expected);
    if (!expected) EXPECT_EQ(expected.error(), failure);
    EXPECT_TRUE(file.write_channel().is_idle());
}

TEST_F(HeldFileTest, CancelledWriteKeepsItsFrameAliveUntilCompletion) {
    auto owner = std::make_shared<int>(7);
    std::weak_ptr<int> weak = owner;
    auto task = scheduler.Spawn(OwnedWrite(file, owner));
    owner.reset();
    scheduler.Run();
    task.Cancel();
    scheduler.Run();
    EXPECT_FALSE(weak.expired());
    EXPECT_FALSE(scheduler.is_empty());
    auto& payload = file.write_channel().Submit();
    payload.header();
    payload.CompleteWrite(4);
    file.write_channel().Complete();
    scheduler.Run();
    EXPECT_TRUE(weak.expired());
    EXPECT_TRUE(scheduler.is_empty());
}

TEST_F(HeldFileTest, CancelledReadKeepsItsFrameAliveUntilCompletion) {
    auto owner = std::make_shared<int>(7);
    std::weak_ptr<int> weak = owner;
    auto task = scheduler.Spawn(OwnedRead(file, owner));
    owner.reset();
    scheduler.Run();
    task.Cancel();
    scheduler.Run();
    EXPECT_FALSE(weak.expired());
    EXPECT_FALSE(scheduler.is_empty());
    auto& payload = file.read_channel().Submit();
    payload.header();
    payload.CompleteRead(0);
    file.read_channel().Complete();
    scheduler.Run();
    EXPECT_TRUE(weak.expired());
    EXPECT_TRUE(scheduler.is_empty());
}

TEST_F(HeldFileTest, MixedOperationsDoNotBatchAcrossTheOtherDirection) {
    std::array<char, 1> first_buffer{}, second_buffer{};
    Outcome first, middle, last;
    scheduler.Spawn(HeldRead(file, first_buffer, 0, first));
    scheduler.Spawn(HeldWrite(file, "x", 0, middle));
    scheduler.Spawn(HeldRead(file, second_buffer, 1, last));
    scheduler.Run();

    auto& reads = file.read_channel().Submit();
    EXPECT_EQ(reads.header().size(), 1U);
    EXPECT_EQ(file.write_channel().Submit().size(), 0U);
    reads.CompleteRead(1);
    file.read_channel().Complete();
    EXPECT_EQ(file.read_channel().Submit().size(), 0U);

    auto& writes = file.write_channel().Submit();
    EXPECT_EQ(writes.header().size(), 1U);
    writes.CompleteWrite(1);
    file.write_channel().Complete();
    auto& remaining = file.read_channel().Submit();
    EXPECT_EQ(remaining.header().size(), 1U);
    remaining.CompleteRead(1);
    file.read_channel().Complete();
    scheduler.Run();
    EXPECT_TRUE(first.finished);
    EXPECT_TRUE(middle.finished);
    EXPECT_TRUE(last.finished);
}

TEST_F(HeldFileTest, ContiguousBatchesRespectTheNativeVectorLimit) {
#if defined(IOV_MAX)
    constexpr std::size_t count = IOV_MAX + 3;
#else
    constexpr std::size_t count = 19;
#endif
    const std::string bytes(count, 'x');
    std::vector<Outcome> outcomes(count);
    for (std::size_t i = 0; i < count; ++i)
        scheduler.Spawn(HeldWrite(file, std::string_view(bytes.data() + i, 1), i, outcomes[i]));
    scheduler.Run();

    auto& payload = file.write_channel().Submit();
    const auto first_count = payload.header().size();
    ASSERT_LT(first_count, count);
    payload.CompleteWrite(first_count);
    file.write_channel().Complete();
    EXPECT_EQ(payload.header().size(), count - first_count);
    payload.CompleteWrite(count - first_count);
    file.write_channel().Complete();
    scheduler.Run();
    for (const auto& outcome : outcomes) {
        EXPECT_TRUE(outcome.finished);
        EXPECT_FALSE(outcome.error);
        EXPECT_EQ(outcome.bytes, 1U);
    }
}
}  // namespace
