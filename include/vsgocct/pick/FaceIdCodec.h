#pragma once

#include <cstdint>

#include <vsg/maths/vec3.h>

namespace vsgocct::pick
{
inline vsg::vec3 encodeFaceId(uint32_t faceId)
{
    uint8_t r = static_cast<uint8_t>((faceId >> 16) & 0xFF);
    uint8_t g = static_cast<uint8_t>((faceId >> 8) & 0xFF);
    uint8_t b = static_cast<uint8_t>(faceId & 0xFF);
    return vsg::vec3(r / 255.0f, g / 255.0f, b / 255.0f);
}

inline uint32_t decodeFaceId(const vsg::vec3& color)
{
    auto r = static_cast<uint8_t>(color.x * 255.0f + 0.5f);
    auto g = static_cast<uint8_t>(color.y * 255.0f + 0.5f);
    auto b = static_cast<uint8_t>(color.z * 255.0f + 0.5f);
    return (static_cast<uint32_t>(r) << 16) | (static_cast<uint32_t>(g) << 8) | b;
}

inline uint32_t decodeFaceIdFromBytes(uint8_t r, uint8_t g, uint8_t b)
{
    return (static_cast<uint32_t>(r) << 16) | (static_cast<uint32_t>(g) << 8) | b;
}
} // namespace vsgocct::pick
