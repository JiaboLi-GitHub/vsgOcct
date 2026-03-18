#pragma once

#include <filesystem>

#include <vsgocct/mesh/ShapeMesher.h>
#include <vsgocct/mesh/StlMeshBuilder.h>
#include <vsgocct/scene/SceneBuilder.h>

namespace vsgocct
{
scene::AssemblySceneData loadScene(
    const std::filesystem::path& modelFile,
    const scene::SceneOptions& sceneOptions = {});

scene::AssemblySceneData loadStepScene(
    const std::filesystem::path& stepFile,
    const mesh::MeshOptions& meshOptions = {},
    const scene::SceneOptions& sceneOptions = {});

scene::AssemblySceneData loadStlScene(
    const std::filesystem::path& stlFile,
    const mesh::StlMeshOptions& stlOptions = {},
    const scene::SceneOptions& sceneOptions = {});
} // namespace vsgocct
