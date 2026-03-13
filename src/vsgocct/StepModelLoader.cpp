#include <vsgocct/StepModelLoader.h>

#include <vsgocct/cad/StepReader.h>
#include <vsgocct/mesh/ShapeMesher.h>
#include <vsgocct/scene/SceneBuilder.h>

namespace vsgocct
{
StepSceneData loadStepScene(const std::filesystem::path& stepFile)
{
    auto assemblyData = cad::readStep(stepFile);

    // TODO(M1b): Walk assemblyData.roots and triangulate each Part node.
    // For now, triangulate the first root's shape to keep existing callers working.
    TopoDS_Shape firstShape;
    for (const auto& root : assemblyData.roots)
    {
        if (!root.shape.IsNull())
        {
            firstShape = root.shape;
            break;
        }
        // If root is an assembly, find first Part child
        std::function<TopoDS_Shape(const cad::ShapeNode&)> findFirst =
            [&](const cad::ShapeNode& node) -> TopoDS_Shape
        {
            if (node.type == cad::ShapeNodeType::Part && !node.shape.IsNull())
                return node.shape;
            for (const auto& child : node.children)
            {
                auto s = findFirst(child);
                if (!s.IsNull()) return s;
            }
            return {};
        };
        firstShape = findFirst(root);
        if (!firstShape.IsNull()) break;
    }

    auto meshResult = mesh::triangulate(firstShape);
    return scene::buildScene(meshResult);
}
} // namespace vsgocct
