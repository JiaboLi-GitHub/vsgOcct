#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include <TopLoc_Location.hxx>
#include <TopoDS_Shape.hxx>

#include <vsgocct/cad/MaterialTypes.h>

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
