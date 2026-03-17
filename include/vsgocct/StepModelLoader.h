#pragma once

#include <filesystem>

#include <vsgocct/scene/SceneBuilder.h>

namespace vsgocct
{
scene::AssemblySceneData loadStepScene(
    const std::filesystem::path& stepFile,
    const mesh::MeshOptions& meshOptions = {},
    const scene::SceneOptions& sceneOptions = {});
} // namespace vsgocct
