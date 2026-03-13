#include <vsgocct/StepModelLoader.h>

#include <vsgocct/cad/StepReader.h>
#include <vsgocct/scene/SceneBuilder.h>

namespace vsgocct
{
scene::AssemblySceneData loadStepScene(const std::filesystem::path& stepFile)
{
    auto assembly = cad::readStep(stepFile);
    return scene::buildAssemblyScene(assembly);
}
} // namespace vsgocct
