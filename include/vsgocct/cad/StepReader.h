#pragma once

#include <array>
#include <filesystem>
#include <string>
#include <vector>

#include <TopLoc_Location.hxx>
#include <TopoDS_Shape.hxx>

namespace vsgocct::cad
{
struct ReaderOptions
{
};

enum class ShapeNodeType
{
    Assembly, // Has children, no geometry
    Part      // Leaf node, holds actual TopoDS_Shape
};

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

struct ShapeNode
{
    ShapeNodeType type = ShapeNodeType::Part;
    std::string name;
    ShapeNodeColor color;
    ShapeVisualMaterial visualMaterial;
    TopoDS_Shape shape;
    TopLoc_Location location;
    std::vector<ShapeNode> children;
};

struct AssemblyData
{
    std::vector<ShapeNode> roots;
};

AssemblyData readStep(const std::filesystem::path& stepFile,
                      const ReaderOptions& options = {});
} // namespace vsgocct::cad
