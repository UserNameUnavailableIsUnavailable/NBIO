#include <gtest/gtest.h>

#include <nbio/fs/payload.hpp>
#include <nbio/net/payload.hpp>

TEST(ScatterPayload, FileQueuesPreserveOrderAndPartialBuffers) {
    nbio::fs::detail::IOVectorPayload<void> payload;
    char first[5]{};
    char second[3]{};
    nbio::fs::Transmission a{.buffer = first};
    nbio::fs::Transmission b{.buffer = second};
    payload.Submit(&a);
    auto& initial = payload.header();
    ASSERT_EQ(initial.size(), 1);
    EXPECT_EQ(initial[0].buffers, first);
    EXPECT_EQ(initial[0].count, 5);

    a.buffer = a.buffer.subspan(2);
    payload.Submit(&b);
    auto& batch = payload.header();
    ASSERT_EQ(batch.size(), 2);
    EXPECT_EQ(batch[0].buffers, first + 2);
    EXPECT_EQ(batch[0].count, 3);
    EXPECT_EQ(batch[1].buffers, second);
    payload.Complete();
    EXPECT_EQ(payload.NextCompletion(), &a);
    payload.Conclude();
    EXPECT_EQ(payload.NextSubmission(), &b);
    EXPECT_EQ(payload.header().size(), 1);
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
