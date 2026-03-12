#pragma once

#include <filesystem>

#include <vsgocct/StepSceneData.h>

namespace vsgocct
{
StepSceneData loadStepScene(const std::filesystem::path& stepFile);
} // namespace vsgocct
