#include <vsgocct/ModelLoader.h>

#include <vsgocct/cad/StepReader.h>
#include <vsgocct/cad/StlReader.h>
#include <vsgocct/mesh/StlMeshBuilder.h>
#include <vsgocct/scene/SceneBuilder.h>

#include <algorithm>
#include <cctype>
#include <stdexcept>

namespace vsgocct
{
namespace
{
std::string toLower(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return s;
}
} // namespace

scene::AssemblySceneData loadScene(
    const std::filesystem::path& modelFile,
    const scene::SceneOptions& sceneOptions)
{
    const auto ext = toLower(modelFile.extension().u8string());

    if (ext == ".step" || ext == ".stp")
    {
        return loadStepScene(modelFile, {}, sceneOptions);
    }

    if (ext == ".stl")
    {
        return loadStlScene(modelFile, {}, sceneOptions);
    }

    throw std::runtime_error("Unsupported file format: " + ext);
}

scene::AssemblySceneData loadStepScene(
    const std::filesystem::path& stepFile,
    const mesh::MeshOptions& meshOptions,
    const scene::SceneOptions& sceneOptions)
{
    auto assembly = cad::readStep(stepFile);
    return scene::buildAssemblyScene(assembly, meshOptions, sceneOptions);
}

scene::AssemblySceneData loadStlScene(
    const std::filesystem::path& stlFile,
    const mesh::StlMeshOptions& stlOptions,
    const scene::SceneOptions& sceneOptions)
{
    auto stlData = cad::readStl(stlFile);
    auto meshResult = mesh::buildStlMesh(stlData.triangulation, stlOptions);

    cad::ShapeNodeColor defaultColor;
    cad::ShapeVisualMaterial defaultMaterial;

    auto partNode = scene::buildPartScene(
        0, stlData.name, meshResult,
        defaultColor, defaultMaterial, sceneOptions);

    std::vector<scene::PartSceneNode> parts;
    parts.push_back(std::move(partNode));
    return scene::assembleScene(std::move(parts), sceneOptions);
}
} // namespace vsgocct
