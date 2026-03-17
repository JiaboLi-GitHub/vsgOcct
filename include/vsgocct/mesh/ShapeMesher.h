#pragma once

#include <cstdint>
#include <cstddef>
#include <vector>

#include <TopoDS_Shape.hxx>
#include <vsg/maths/vec3.h>

namespace vsgocct::mesh
{
struct PointSpan
{
    uint32_t vertexId = 0;
    uint32_t firstPoint = 0;
    uint32_t pointCount = 0;
};

struct LineSpan
{
    uint32_t edgeId = 0;
    uint32_t firstSegment = 0;
    uint32_t segmentCount = 0;
};

struct FaceSpan
{
    uint32_t faceId = 0;
    uint32_t firstTriangle = 0;
    uint32_t triangleCount = 0;
};

struct MeshOptions
{
    double linearDeflection = 0.0;
    double angularDeflection = 0.35;
    bool relative = false;
};

struct MeshResult
{
    std::vector<vsg::vec3> pointPositions;
    std::vector<PointSpan> pointSpans;
    std::size_t pointCount = 0;

    std::vector<vsg::vec3> linePositions;
    std::vector<LineSpan> lineSpans;
    std::size_t lineSegmentCount = 0;

    std::vector<vsg::vec3> facePositions;
    std::vector<vsg::vec3> faceNormals;
    std::vector<FaceSpan> faceSpans;
    std::size_t triangleCount = 0;

    vsg::dvec3 boundsMin;
    vsg::dvec3 boundsMax;

    bool hasGeometry() const
    {
        return pointCount > 0 || lineSegmentCount > 0 || triangleCount > 0;
    }
};

MeshResult triangulate(const TopoDS_Shape& shape,
                       const MeshOptions& options = {});
} // namespace vsgocct::mesh
