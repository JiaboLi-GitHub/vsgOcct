#include <gtest/gtest.h>

#include <vsgocct/pick/FaceIdCodec.h>

using namespace vsgocct::pick;

TEST(FaceIdCodec, EncodeDecodeRoundTrip)
{
    for (uint32_t id : {1u, 2u, 255u, 256u, 65535u, 65536u, 16777215u})
    {
        auto color = encodeFaceId(id);
        EXPECT_EQ(decodeFaceId(color), id) << "Failed round-trip for id=" << id;
    }
}

TEST(FaceIdCodec, BackgroundIsZero)
{
    auto color = encodeFaceId(0);
    EXPECT_FLOAT_EQ(color.x, 0.0f);
    EXPECT_FLOAT_EQ(color.y, 0.0f);
    EXPECT_FLOAT_EQ(color.z, 0.0f);

    EXPECT_EQ(decodeFaceId(vsg::vec3(0.0f, 0.0f, 0.0f)), 0u);
}

TEST(FaceIdCodec, EncodeId1)
{
    auto color = encodeFaceId(1);
    EXPECT_FLOAT_EQ(color.x, 0.0f);
    EXPECT_FLOAT_EQ(color.y, 0.0f);
    EXPECT_NEAR(color.z, 1.0f / 255.0f, 1e-6f);
}

TEST(FaceIdCodec, EncodeMaxId)
{
    auto color = encodeFaceId(16777215u);
    EXPECT_NEAR(color.x, 1.0f, 1e-6f);
    EXPECT_NEAR(color.y, 1.0f, 1e-6f);
    EXPECT_NEAR(color.z, 1.0f, 1e-6f);
}

TEST(FaceIdCodec, DecodeFromUint8)
{
    EXPECT_EQ(decodeFaceIdFromBytes(0, 1, 42), (1u << 8) | 42u);
}
