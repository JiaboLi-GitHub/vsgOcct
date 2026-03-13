#pragma once

#include <filesystem>

#include <vsgocct/scene/SceneBuilder.h>

namespace vsgocct
{
scene::AssemblySceneData loadStepScene(const std::filesystem::path& stepFile);
} // namespace vsgocct
