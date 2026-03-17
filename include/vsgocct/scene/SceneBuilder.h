#pragma once

#include <cstdint>
#include <cstddef>
#include <limits>
#include <string>
#include <vector>

#include <vsg/all.h>

#include <vsgocct/cad/StepReader.h>
#include <vsgocct/mesh/ShapeMesher.h>
#include <vsgocct/selection/SelectionToken.h>

namespace vsgocct::scene
{
enum class ShadingMode
{
    Legacy,
    Pbr
};

enum class MaterialPreset
{
    Imported,
    Iron,
    Copper,
    Gold,
    Wood,
    Acrylic
};

struct SceneOptions
{
    ShadingMode shadingMode = ShadingMode::Legacy;
    bool addHeadlight = true;
};

struct PartSceneNode
{
    uint32_t partId = 0;
    std::string name;
    vsg::ref_ptr<vsg::Switch> switchNode;
    vsg::vec4 baseColor;
    cad::ShapeVisualMaterial importedMaterial;
    cad::ShapeVisualMaterial visualMaterial;
    vsg::ref_ptr<vsg::PbrMaterialValue> pbrMaterialValue;
    vsg::ref_ptr<vsg::vec4Array> faceColors;
    vsg::ref_ptr<vsg::vec3Array> lineColors;
    vsg::ref_ptr<vsg::vec3Array> pointColors;
    std::vector<mesh::PointSpan> pointSpans;
    std::vector<mesh::LineSpan> lineSpans;
    std::vector<mesh::FaceSpan> faceSpans;
};

inline constexpr uint32_t InvalidPartId = std::numeric_limits<uint32_t>::max();

struct AssemblySceneData
{
    vsg::ref_ptr<vsg::Node> scene;
    std::vector<PartSceneNode> parts;
    ShadingMode shadingMode = ShadingMode::Legacy;
    MaterialPreset materialPreset = MaterialPreset::Imported;

    vsg::dvec3 center;
    double radius = 1.0;

    std::size_t totalTriangleCount = 0;
    std::size_t totalLineSegmentCount = 0;
    std::size_t totalPointCount = 0;

    uint32_t selectedPartId = InvalidPartId;
    selection::SelectionToken selectedToken;
    selection::SelectionToken hoverToken;
};

AssemblySceneData buildAssemblyScene(
    const cad::AssemblyData& assembly,
    const mesh::MeshOptions& meshOptions = {},
    const SceneOptions& sceneOptions = {});

PartSceneNode* findPart(AssemblySceneData& sceneData, uint32_t partId);
const PartSceneNode* findPart(const AssemblySceneData& sceneData, uint32_t partId);

const char* materialPresetName(MaterialPreset preset);
cad::ShapeVisualMaterial makeMaterialPreset(MaterialPreset preset);
bool applyMaterialPreset(AssemblySceneData& sceneData, MaterialPreset preset);

bool setSelection(AssemblySceneData& sceneData, const selection::SelectionToken& token);
void clearSelection(AssemblySceneData& sceneData);

bool setHoverSelection(AssemblySceneData& sceneData, const selection::SelectionToken& token);
void clearHoverSelection(AssemblySceneData& sceneData);

bool setSelectedPart(AssemblySceneData& sceneData, uint32_t partId);
void clearSelectedPart(AssemblySceneData& sceneData);
} // namespace vsgocct::scene
