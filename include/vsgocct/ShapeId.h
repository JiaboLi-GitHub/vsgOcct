#pragma once

#include <cstdint>
#include <functional>
#include <string_view>

namespace vsgocct
{
struct ShapeId
{
    std::uint64_t value = 0;

    bool operator==(const ShapeId& other) const { return value == other.value; }
    bool operator!=(const ShapeId& other) const { return value != other.value; }
    explicit operator bool() const { return value != 0; }
};

struct FaceId
{
    std::uint64_t value = 0;

    bool operator==(const FaceId& other) const { return value == other.value; }
    bool operator!=(const FaceId& other) const { return value != other.value; }
    explicit operator bool() const { return value != 0; }
};

struct EdgeId
{
    std::uint64_t value = 0;
    // Reserved for future use. Not populated in M1b.

    bool operator==(const EdgeId& other) const { return value == other.value; }
    bool operator!=(const EdgeId& other) const { return value != other.value; }
    explicit operator bool() const { return value != 0; }
};

namespace detail
{
constexpr std::uint64_t fnv1aOffset = 14695981039346656037ULL;
constexpr std::uint64_t fnv1aPrime  = 1099511628211ULL;

inline std::uint64_t fnv1a64(std::string_view str)
{
    std::uint64_t hash = fnv1aOffset;
    for (char c : str)
    {
        hash ^= static_cast<std::uint64_t>(static_cast<unsigned char>(c));
        hash *= fnv1aPrime;
    }
    return hash;
}

inline std::uint64_t fnv1a64(std::uint64_t base, std::uint32_t index)
{
    std::uint64_t hash = base;
    for (int i = 0; i < 4; ++i)
    {
        hash ^= static_cast<std::uint64_t>(index & 0xFF);
        hash *= fnv1aPrime;
        index >>= 8;
    }
    return hash;
}
} // namespace detail
} // namespace vsgocct

template<> struct std::hash<vsgocct::ShapeId>
{
    std::size_t operator()(const vsgocct::ShapeId& id) const noexcept
    {
        return std::hash<std::uint64_t>{}(id.value);
    }
};

template<> struct std::hash<vsgocct::FaceId>
{
    std::size_t operator()(const vsgocct::FaceId& id) const noexcept
    {
        return std::hash<std::uint64_t>{}(id.value);
    }
};

template<> struct std::hash<vsgocct::EdgeId>
{
    std::size_t operator()(const vsgocct::EdgeId& id) const noexcept
    {
        return std::hash<std::uint64_t>{}(id.value);
    }
};
