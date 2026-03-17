#pragma once

#include <cstdint>
#include <optional>

#include <vsg/app/Camera.h>

#include <vsgocct/scene/SceneBuilder.h>
#include <vsgocct/selection/SelectionToken.h>

namespace vsgocct::selection
{
std::optional<PickResult> pick(const vsg::Camera& camera,
                               const scene::AssemblySceneData& sceneData,
                               int32_t x,
                               int32_t y);
} // namespace vsgocct::selection
