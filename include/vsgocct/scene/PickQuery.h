#pragma once

#include <optional>

#include <vsg/all.h>

#include <vsgocct/ShapeId.h>
#include <vsgocct/scene/SceneBuilder.h>

namespace vsgocct::scene
{
struct PickResult
{
    ShapeId shapeId;
    FaceId faceId;
    vsg::dvec3 worldPoint;
};

std::optional<PickResult> pickScene(
    const AssemblySceneData& sceneData,
    const vsg::ref_ptr<vsg::Camera>& camera,
    double screenX, double screenY);
} // namespace vsgocct::scene
