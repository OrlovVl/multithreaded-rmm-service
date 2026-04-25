#include <gtest/gtest.h>

#include "rmm/shared/protocol.hpp"

TEST(Protocol, EncodeDecodeRoundTrip)
{
    const rmm::shared::WireMessage input{"METRICS", R"({"nodeName":"n1","cpuUsage":1.0})"};

    const auto encoded = rmm::shared::encodeWireMessage(input);
    const auto decoded = rmm::shared::decodeWireMessage(encoded);

    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->type, input.type);
    EXPECT_EQ(decoded->payload, input.payload);
}