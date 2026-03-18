#pragma once

#include <array>

namespace vsgocct::cad
{
struct ShapeNodeColor
{
    float r = 0.74f;
    float g = 0.79f;
    float b = 0.86f;
    bool isSet = false;
};

enum class ShapeVisualMaterialSource
{
    Default,
    ColorFallback,
    Pbr
};

struct ShapeVisualMaterial
{
    std::array<float, 4> baseColorFactor{0.74f, 0.79f, 0.86f, 1.0f};
    std::array<float, 3> emissiveFactor{0.0f, 0.0f, 0.0f};
    float metallicFactor = 0.0f;
    float roughnessFactor = 0.65f;
    float alphaCutoff = 0.5f;
    bool alphaMask = false;
    bool doubleSided = true;
    bool hasPbr = false;
    ShapeVisualMaterialSource source = ShapeVisualMaterialSource::Default;
};
} // namespace vsgocct::cad
