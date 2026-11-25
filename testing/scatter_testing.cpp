#include <gtest/gtest.h>

#include <nbio/fs/payload.hpp>
#include <nbio/net/payload.hpp>

TEST(ScatterPayload, FileQueuesPreserveOrderAndPartialBuffers) {
    nbio::fs::detail::IOVectorPayload<void> payload;
    char first[5]{};
    char second[3]{};
    nbio::fs::Transmission a{.buffer = first, .offset = 0};
    nbio::fs::Transmission b{.buffer = second, .offset = 5};
    payload.Submit(&a);
    auto& initial = payload.header();
    ASSERT_EQ(initial.size(), 1);
    EXPECT_EQ(initial[0].buffers, first);
    EXPECT_EQ(initial[0].count, 5);

    // A range that starts exactly where the previous one ends joins its vectors.
    payload.Submit(&b);
    auto& batch = payload.header();
    ASSERT_EQ(batch.size(), 2);
    EXPECT_EQ(batch[0].buffers, first);
    EXPECT_EQ(batch[0].count, 5);
    EXPECT_EQ(batch[1].buffers, second);
    EXPECT_EQ(batch[1].count, 3);
    EXPECT_EQ(payload.offset(), 0);

    // A gap must not be coalesced: one starting offset cannot describe it.
    char third[2]{};
    nbio::fs::Transmission c{.buffer = third, .offset = 20};
    payload.Submit(&c);
    EXPECT_EQ(payload.header().size(), 2);

    // A short write completes what it filled and leaves the rest at its new offset.
    payload.CompleteWrite(7);
    EXPECT_EQ(payload.NextCompletion(), &a);
    payload.Conclude();
    EXPECT_EQ(payload.NextCompletion(), nullptr);
    EXPECT_EQ(payload.NextSubmission(), &b);
    EXPECT_EQ(b.status, nbio::fs::OperationStatus::kPending);
    EXPECT_EQ(b.bytes, 2);
    EXPECT_EQ(b.offset, 7);
    EXPECT_EQ(b.buffer.data(), second + 2);
    EXPECT_EQ(b.buffer.size(), 1);
    EXPECT_EQ(payload.header().size(), 1);
    EXPECT_EQ(payload.offset(), 7);
}

TEST(ScatterPayload, SocketUsesTheSameScatterAndKeepsNativeStorage) {
    nbio::net::detail::MessagePayload<void> payload;
    char buffer[7]{};
    nbio::net::Transmission transmission{.buffer = buffer};
    payload.submit(&transmission);
    const std::vector<nbio::core::Scatter>& batch = payload.header();
    ASSERT_EQ(batch.size(), 1);
    EXPECT_EQ(batch[0].buffers, buffer);
    EXPECT_EQ(batch[0].count, sizeof(buffer));
#if defined(__linux__)
    const auto& native = payload.native_header();
    ASSERT_EQ(native.msg_iovlen, 1);
    EXPECT_EQ(native.msg_iov[0].iov_base, buffer);
    EXPECT_EQ(native.msg_iov[0].iov_len, sizeof(buffer));
    nbio::fs::detail::IOVectorPayload<void> file;
    nbio::fs::Transmission file_transmission{.buffer = buffer};
    file.Submit(&file_transmission);
    const auto& vectors = file.native_header();
    ASSERT_EQ(vectors.size(), 1);
    EXPECT_EQ(vectors[0].iov_base, buffer);
    EXPECT_EQ(vectors[0].iov_len, sizeof(buffer));
#endif
}
