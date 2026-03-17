#include <vsgocct/selection/ScenePicker.h>

#include <vsg/utils/LineSegmentIntersector.h>

#include <algorithm>

namespace vsgocct::selection
{
namespace
{
constexpr const char* PART_ID_KEY = "vsgocct.partId";
constexpr const char* PRIMITIVE_KIND_KEY = "vsgocct.primitiveKind";

PrimitiveKind decodePrimitiveKind(uint32_t rawValue)
{
    switch (rawValue)
    {
    case static_cast<uint32_t>(PrimitiveKind::Part):
        return PrimitiveKind::Part;
    case static_cast<uint32_t>(PrimitiveKind::Face):
        return PrimitiveKind::Face;
    case static_cast<uint32_t>(PrimitiveKind::Edge):
        return PrimitiveKind::Edge;
    case static_cast<uint32_t>(PrimitiveKind::Vertex):
        return PrimitiveKind::Vertex;
    default:
        return PrimitiveKind::None;
    }
}

PrimitiveKind findPrimitiveKind(const vsg::LineSegmentIntersector::Intersection& intersection)
{
    for (auto itr = intersection.nodePath.rbegin(); itr != intersection.nodePath.rend(); ++itr)
    {
        uint32_t rawValue = 0;
        if ((*itr)->getValue(PRIMITIVE_KIND_KEY, rawValue))
        {
            return decodePrimitiveKind(rawValue);
        }
    }

    return PrimitiveKind::Part;
}

bool findPartId(const vsg::LineSegmentIntersector::Intersection& intersection, uint32_t& partId)
{
    for (auto itr = intersection.nodePath.rbegin(); itr != intersection.nodePath.rend(); ++itr)
    {
        if ((*itr)->getValue(PART_ID_KEY, partId))
        {
            return true;
        }
    }

    return false;
}

uint32_t primitiveIndexFromIntersection(const vsg::LineSegmentIntersector::Intersection& intersection,
                                        PrimitiveKind kind)
{
    if (intersection.indexRatios.empty())
    {
        return 0;
    }

    uint32_t minIndex = intersection.indexRatios.front().index;
    for (const auto& indexRatio : intersection.indexRatios)
    {
        minIndex = std::min(minIndex, indexRatio.index);
    }

    switch (kind)
    {
    case PrimitiveKind::Face:
        return minIndex / 3u;
    case PrimitiveKind::Edge:
        return minIndex / 2u;
    case PrimitiveKind::Vertex:
        return minIndex;
    case PrimitiveKind::Part:
    case PrimitiveKind::None:
    default:
        return 0;
    }
}

uint32_t resolvePrimitiveId(const scene::PartSceneNode& part,
                            PrimitiveKind kind,
                            uint32_t primitiveIndex)
{
    switch (kind)
    {
    case PrimitiveKind::Face:
        for (const auto& span : part.faceSpans)
        {
            const uint32_t spanEnd = span.firstTriangle + span.triangleCount;
            if (primitiveIndex >= span.firstTriangle && primitiveIndex < spanEnd)
            {
                return span.faceId;
            }
        }
        return primitiveIndex;

    case PrimitiveKind::Edge:
        for (const auto& span : part.lineSpans)
        {
            const uint32_t spanEnd = span.firstSegment + span.segmentCount;
            if (primitiveIndex >= span.firstSegment && primitiveIndex < spanEnd)
            {
                return span.edgeId;
            }
        }
        return primitiveIndex;

    case PrimitiveKind::Vertex:
        for (const auto& span : part.pointSpans)
        {
            const uint32_t spanEnd = span.firstPoint + span.pointCount;
            if (primitiveIndex >= span.firstPoint && primitiveIndex < spanEnd)
            {
                return span.vertexId;
            }
        }
        return primitiveIndex;

    case PrimitiveKind::Part:
        return part.partId;

    case PrimitiveKind::None:
    default:
        return InvalidSelectionId;
    }
}
} // namespace

std::optional<PickResult> pick(const vsg::Camera& camera,
                               const scene::AssemblySceneData& sceneData,
                               int32_t x,
                               int32_t y)
{
    if (!sceneData.scene)
    {
        return std::nullopt;
    }

    auto intersector = vsg::LineSegmentIntersector(camera, x, y);
    sceneData.scene->accept(intersector);
    if (intersector.intersections.empty())
    {
        return std::nullopt;
    }

    const auto bestIntersectionIt = std::min_element(
        intersector.intersections.begin(),
        intersector.intersections.end(),
        [](const auto& lhs, const auto& rhs)
        {
            return lhs->ratio < rhs->ratio;
        });
    if (bestIntersectionIt == intersector.intersections.end() || !(*bestIntersectionIt))
    {
        return std::nullopt;
    }

    const auto& intersection = *(*bestIntersectionIt);

    uint32_t partId = scene::InvalidPartId;
    if (!findPartId(intersection, partId))
    {
        return std::nullopt;
    }

    const scene::PartSceneNode* part = scene::findPart(sceneData, partId);
    if (!part)
    {
        return std::nullopt;
    }

    PrimitiveKind primitiveKind = findPrimitiveKind(intersection);
    const uint32_t primitiveIndex = primitiveIndexFromIntersection(intersection, primitiveKind);

    PickResult result;
    result.token.partId = part->partId;
    result.token.kind = primitiveKind;
    result.token.primitiveId = resolvePrimitiveId(*part, primitiveKind, primitiveIndex);
    result.partName = part->name;
    result.primitiveIndex = primitiveIndex;
    result.worldIntersection = intersection.worldIntersection;
    result.rayRatio = intersection.ratio;
    return result;
}
} // namespace vsgocct::selection
