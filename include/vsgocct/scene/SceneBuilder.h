#pragma once

#include <cstddef>
#include <string>
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
};

AssemblySceneData buildAssemblyScene(
    const cad::AssemblyData& assembly,
    const mesh::MeshOptions& meshOptions = {},
    const SceneOptions& sceneOptions = {});
} // namespace vsgocct::scene
