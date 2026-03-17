#pragma once

#include <cstdint>
#include <limits>
#include <string>

#include <vsg/maths/vec3.h>

namespace vsgocct::selection
{
inline constexpr uint32_t InvalidSelectionId = std::numeric_limits<uint32_t>::max();

enum class PrimitiveKind : uint32_t
{
    None = 0,
    Part = 1,
    Face = 2,
    Edge = 3,
    Vertex = 4
};

struct SelectionToken
{
    uint32_t partId = InvalidSelectionId;
    PrimitiveKind kind = PrimitiveKind::None;
    uint32_t primitiveId = InvalidSelectionId;

    explicit operator bool() const
    {
        return partId != InvalidSelectionId && kind != PrimitiveKind::None;
    }
};

struct PickResult
{
    SelectionToken token;
    std::string partName;
    uint32_t primitiveIndex = 0;
    vsg::dvec3 worldIntersection;
    double rayRatio = 0.0;

    explicit operator bool() const
    {
        return static_cast<bool>(token);
    }
};
} // namespace vsgocct::selection
