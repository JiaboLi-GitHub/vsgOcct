#pragma once

#include <vsgocct/StepSceneData.h>
#include <vsgocct/mesh/ShapeMesher.h>

namespace vsgocct::scene
{
struct SceneOptions
{
    bool pointsVisible = true;
    bool linesVisible = true;
    bool facesVisible = true;
};

StepSceneData buildScene(const mesh::MeshResult& meshResult,
                         const SceneOptions& options = {});
} // namespace vsgocct::scene
