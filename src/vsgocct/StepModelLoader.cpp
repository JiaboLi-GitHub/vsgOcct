#include <vsgocct/StepModelLoader.h>

#include <vsgocct/cad/StepReader.h>
#include <vsgocct/mesh/ShapeMesher.h>
#include <vsgocct/scene/SceneBuilder.h>

namespace vsgocct
{
StepSceneData loadStepScene(const std::filesystem::path& stepFile)
{
    auto shapeData = cad::readStep(stepFile);
    auto meshResult = mesh::triangulate(shapeData.shape);
    return scene::buildScene(meshResult);
}
} // namespace vsgocct
