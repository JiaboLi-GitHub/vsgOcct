#pragma once

#include <cstddef>
#include <filesystem>

#include <vsg/all.h>

namespace vsgocct
{
struct StepSceneData
{
    vsg::ref_ptr<vsg::Node> scene;
    vsg::dvec3 center;
    double radius = 1.0;
    std::size_t triangleCount = 0;
};

StepSceneData loadStepScene(const std::filesystem::path& stepFile);
} // namespace vsgocct
