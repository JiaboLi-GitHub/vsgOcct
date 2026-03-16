#pragma once

#include <cstddef>
#include <string>
#include <unordered_map>
#include <vector>

#include <vsg/all.h>

#include <vsgocct/cad/StepReader.h>
#include <vsgocct/mesh/ShapeMesher.h>

namespace vsgocct::scene
{
struct SceneOptions
{
    // Reserved for future options
};

struct PartSceneNode
{
    std::string name;
    vsg::ref_ptr<vsg::Switch> switchNode;
    vsg::ref_ptr<vsg::Switch> pickSwitchNode;
};

struct FaceInfo
{
    uint32_t faceId = 0;
    std::string partName;
    uint32_t partIndex = 0;
    vsg::vec3 faceNormal{0.0f, 0.0f, 0.0f};
};

struct PickResult
{
    uint32_t faceId = 0;
    const FaceInfo* faceInfo = nullptr;
};

struct PartColorRef
{
    vsg::ref_ptr<vsg::vec3Array> colorArray;
    uint32_t vertexOffset = 0;
    uint32_t vertexCount = 0;
};

struct AssemblySceneData
{
    vsg::ref_ptr<vsg::Node> scene;
    std::vector<PartSceneNode> parts;

    vsg::dvec3 center;
    double radius = 1.0;

    std::size_t totalTriangleCount = 0;
    std::size_t totalLineSegmentCount = 0;
    std::size_t totalPointCount = 0;

    vsg::ref_ptr<vsg::Node> pickScene;
    std::unordered_map<uint32_t, FaceInfo> faceRegistry;
    std::unordered_map<uint32_t, PartColorRef> faceColorRefs;
};

AssemblySceneData buildAssemblyScene(
    const cad::AssemblyData& assembly,
    const mesh::MeshOptions& meshOptions = {},
    const SceneOptions& sceneOptions = {});
} // namespace vsgocct::scene
