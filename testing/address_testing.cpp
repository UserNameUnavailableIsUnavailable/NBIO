#include <gtest/gtest.h>

#include <nbio/net/address.hpp>
#include <string_view>

namespace {
using nbio::net::Address;
using Family = Address::Family;
}  // namespace

TEST(AddressTesting, ParsesIPv4AndNativePort) {
    auto result = Address::FromIPv4("192.0.2.1", 54321);
    ASSERT_TRUE(result);
    EXPECT_TRUE(result->is_valid());
    EXPECT_EQ(result->family(), Family::kIPv4);
    EXPECT_EQ(result->ip(), "192.0.2.1");
    EXPECT_EQ(result->port(), 54321);
    EXPECT_EQ(result->length(), sizeof(::sockaddr_in));
    EXPECT_EQ(result->storage<::sockaddr_in>()->sin_port, htons(54321));
}

TEST(AddressTesting, ParsesIPv6AndNativePort) {
    auto result = Address::FromIPv6("2001:db8::1", 65535);
    ASSERT_TRUE(result);
    EXPECT_TRUE(result->is_valid());
    EXPECT_EQ(result->family(), Family::kIPv6);
    EXPECT_EQ(result->ip(), "2001:db8::1");
    EXPECT_EQ(result->port(), 65535);
    EXPECT_EQ(result->length(), sizeof(::sockaddr_in6));
    EXPECT_EQ(result->storage<::sockaddr_in6>()->sin6_port, htons(65535));
    EXPECT_EQ(result->storage<::sockaddr_in6>()->sin6_scope_id, 0U);
    EXPECT_EQ(result->storage<::sockaddr_in6>()->sin6_flowinfo, 0U);
}

TEST(AddressTesting, DetectsFamilyAndAcceptsWildcardAddresses) {
    for (auto ip : {"0.0.0.0", "127.0.0.1"}) {
        auto result = Address::FromUnknown(ip, 0);
        ASSERT_TRUE(result);
        EXPECT_EQ(result->family(), Family::kIPv4);
        EXPECT_EQ(result->port(), 0);
    }
    for (auto ip : {"::", "::1", "::ffff:192.0.2.1"}) {
        auto result = Address::FromUnknown(ip, 0);
        ASSERT_TRUE(result);
        EXPECT_EQ(result->family(), Family::kIPv6);
        EXPECT_EQ(result->port(), 0);
    }
    EXPECT_FALSE(Address{}.is_valid());
    EXPECT_EQ(Address{}.family(), Family::kUnspecified);
}

TEST(AddressTesting, RejectsMalformedAndWrongFamilyAddresses) {
    for (auto ip : {"", "hostname", "256.0.0.1", "127.1", "127.0.0.1 ", "2001:::1", "::1%1"}) {
        auto result = Address::FromUnknown(ip, 80);
        ASSERT_FALSE(result) << ip;
        EXPECT_EQ(result.error(), std::errc::invalid_argument);
    }
    const auto v4 = Address::FromIPv4("::1", 80);
    ASSERT_FALSE(v4);
    EXPECT_EQ(v4.error(), std::errc::invalid_argument);
    const auto v6 = Address::FromIPv6("127.0.0.1", 80);
    ASSERT_FALSE(v6);
    EXPECT_EQ(v6.error(), std::errc::invalid_argument);
}

TEST(AddressTesting, HandlesNonTerminatedViewsAndRejectsEmbeddedNulls) {
    constexpr std::string_view v4{"127.0.0.1trailing"};
    auto parsed_v4 = Address::FromIPv4(v4.substr(0, 9), 80);
    ASSERT_TRUE(parsed_v4);
    EXPECT_EQ(parsed_v4->ip(), "127.0.0.1");
    constexpr std::string_view v6{"::1trailing"};
    auto parsed_v6 = Address::FromIPv6(v6.substr(0, 3), 80);
    ASSERT_TRUE(parsed_v6);
    EXPECT_EQ(parsed_v6->ip(), "::1");
    for (auto ip : {std::string_view{"127.0.0.1\0ignored", 17}, std::string_view{"::1\0ignored", 11}}) {
        const auto result = Address::FromUnknown(ip, 80);
        ASSERT_FALSE(result);
        EXPECT_EQ(result.error(), std::errc::invalid_argument);
    }
}
