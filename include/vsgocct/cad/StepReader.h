#pragma once

#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

#include <TopLoc_Location.hxx>
#include <TopoDS_Shape.hxx>

#include <vsgocct/ShapeId.h>

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

struct ShapeNode
{
    ShapeNodeType type = ShapeNodeType::Part;
    std::string name;
    ShapeNodeColor color;
    TopoDS_Shape shape;
    TopLoc_Location location;
    std::vector<ShapeNode> children;
    vsgocct::ShapeId id;
    std::string assemblyPath;
};

struct AssemblyData
{
    std::vector<ShapeNode> roots;
    std::unordered_map<vsgocct::ShapeId, const ShapeNode*> shapeIndex;

    AssemblyData() = default;
    AssemblyData(const AssemblyData&) = delete;
    AssemblyData& operator=(const AssemblyData&) = delete;
    AssemblyData(AssemblyData&&) = default;
    AssemblyData& operator=(AssemblyData&&) = default;
};

AssemblyData readStep(const std::filesystem::path& stepFile,
                      const ReaderOptions& options = {});
} // namespace vsgocct::cad
