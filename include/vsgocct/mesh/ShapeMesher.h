#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include <TopoDS_Shape.hxx>
#include <vsg/maths/vec3.h>

namespace vsgocct::mesh
{
struct MeshOptions
{
    double linearDeflection = 0.0;
    double angularDeflection = 0.35;
    bool relative = false;
};

struct FaceData
{
    uint32_t faceId = 0;
    uint32_t triangleOffset = 0;
    uint32_t triangleCount = 0;
    vsg::vec3 normal{0.0f, 0.0f, 0.0f};
};

struct MeshResult
{
    std::vector<vsg::vec3> pointPositions;
    std::size_t pointCount = 0;

    std::vector<vsg::vec3> linePositions;
    std::size_t lineSegmentCount = 0;

    std::vector<vsg::vec3> facePositions;
    std::vector<vsg::vec3> faceNormals;
    std::size_t triangleCount = 0;

    vsg::dvec3 boundsMin;
    vsg::dvec3 boundsMax;

    std::vector<FaceData> faces;
    std::vector<uint32_t> perTriangleFaceId;

    bool hasGeometry() const
    {
        return pointCount > 0 || lineSegmentCount > 0 || triangleCount > 0;
    }
};

MeshResult triangulate(const TopoDS_Shape& shape, uint32_t& nextFaceId,
                       const MeshOptions& options = {});

inline MeshResult triangulate(const TopoDS_Shape& shape,
                              const MeshOptions& options = {})
{
    uint32_t nextFaceId = 1;
    return triangulate(shape, nextFaceId, options);
}
} // namespace vsgocct::mesh
