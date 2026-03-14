#pragma once

#include <cstddef>
#include <string>
#include <unordered_map>
#include <vector>

#include <vsg/all.h>

#include <vsgocct/ShapeId.h>
#include <vsgocct/cad/StepReader.h>
#include <vsgocct/mesh/ShapeMesher.h>

namespace vsgocct::scene
{
struct SceneIndex
{
    std::unordered_map<ShapeId, vsg::ref_ptr<vsg::Switch>>   shapeToSwitch;
    std::unordered_map<ShapeId, TopoDS_Shape>                shapeToOcct;
    std::unordered_map<FaceId, mesh::FaceMeshRange>          faceToTriangles;

    std::unordered_map<const vsg::Node*, ShapeId>            nodeToShape;

    struct FaceEntry
    {
        std::uint32_t firstTriangle;
        FaceId faceId;
    };
    std::unordered_map<ShapeId, std::vector<FaceEntry>>      shapeFaces;

    const vsg::Switch* findSwitch(ShapeId id) const;
    const TopoDS_Shape* findOcctShape(ShapeId id) const;
    ShapeId findShapeByNode(const vsg::Node* node) const;
    FaceId findFaceByTriangle(ShapeId partId, std::uint32_t triangleIndex) const;
};

struct SceneOptions
{
    // Reserved for future options
};

struct PartSceneNode
{
    std::string name;
    vsg::ref_ptr<vsg::Switch> switchNode;
};

struct AssemblySceneData
{
    vsg::ref_ptr<vsg::Node> scene;
    std::vector<PartSceneNode> parts;

    // Per-geometry-type switch nodes for global visibility control
    std::vector<vsg::ref_ptr<vsg::Switch>> faceSwitches;
    std::vector<vsg::ref_ptr<vsg::Switch>> lineSwitches;
    std::vector<vsg::ref_ptr<vsg::Switch>> pointSwitches;

    vsg::dvec3 center;
    double radius = 1.0;

    std::size_t totalTriangleCount = 0;
    std::size_t totalLineSegmentCount = 0;
    std::size_t totalPointCount = 0;

    SceneIndex index;
};

AssemblySceneData buildAssemblyScene(
    const cad::AssemblyData& assembly,
    const mesh::MeshOptions& meshOptions = {},
    const SceneOptions& sceneOptions = {});
} // namespace vsgocct::scene
