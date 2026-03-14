#include <vsgocct/scene/PickQuery.h>

namespace vsgocct::scene
{
std::optional<PickResult> pickScene(
    const AssemblySceneData& sceneData,
    const vsg::ref_ptr<vsg::Camera>& camera,
    double screenX, double screenY)
{
    if (!sceneData.scene || !camera)
    {
        return std::nullopt;
    }

    auto intersector = vsg::LineSegmentIntersector::create(*camera, screenX, screenY);
    sceneData.scene->accept(*intersector);

    if (intersector->intersections.empty())
    {
        return std::nullopt;
    }

    // Sort by ratio (closest first)
    std::sort(intersector->intersections.begin(),
              intersector->intersections.end(),
              [](const auto& a, const auto& b) { return a->ratio < b->ratio; });

    const auto& closest = intersector->intersections.front();

    // Walk up node path to find a node registered in SceneIndex
    ShapeId shapeId{};
    for (auto it = closest->nodePath.rbegin(); it != closest->nodePath.rend(); ++it)
    {
        shapeId = sceneData.index.findShapeByNode(*it);
        if (shapeId)
        {
            break;
        }
    }

    if (!shapeId)
    {
        return std::nullopt;
    }

    // Extract triangle index from the intersection
    // VSG intersections store the primitive index via indexRatios
    std::uint32_t triangleIndex = 0;
    if (!closest->indexRatios.empty())
    {
        triangleIndex = static_cast<std::uint32_t>(closest->indexRatios.front().index / 3);
    }

    FaceId faceId = sceneData.index.findFaceByTriangle(shapeId, triangleIndex);

    PickResult result;
    result.shapeId = shapeId;
    result.faceId = faceId;
    result.worldPoint = closest->worldIntersection;

    return result;
}
} // namespace vsgocct::scene
