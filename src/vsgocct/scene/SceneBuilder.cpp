#include <vsgocct/scene/SceneBuilder.h>

#include <vsg/all.h>

#include <vsgocct/selection/SelectionToken.h>

#include <algorithm>
#include <cstddef>
#include <limits>
#include <utility>
#include <vector>

namespace vsgocct::scene
{
namespace
{
constexpr const char* PART_ID_KEY = "vsgocct.partId";
constexpr const char* PRIMITIVE_KIND_KEY = "vsgocct.primitiveKind";
const vsg::vec3 BASE_EDGE_COLOR = vsg::vec3(0.12f, 0.25f, 0.40f);
const vsg::vec3 BASE_VERTEX_COLOR = vsg::vec3(0.90f, 0.40f, 0.12f);
const vsg::vec3 SELECTED_PART_COLOR = vsg::vec3(1.0f, 0.92f, 0.18f);
const vsg::vec3 SELECTED_FACE_COLOR = vsg::vec3(1.0f, 0.66f, 0.18f);
const vsg::vec3 SELECTED_EDGE_COLOR = vsg::vec3(0.10f, 0.90f, 1.0f);
const vsg::vec3 SELECTED_VERTEX_COLOR = vsg::vec3(1.0f, 0.15f, 0.55f);

// Per-vertex color approach: color is stored as a vertex attribute (binding 2)
// rather than push constants, because VSG auto-pushes only projection+modelView
// (128 bytes) and custom push constant data requires a custom StateCommand.
// Per-vertex color is simpler, keeps push constants at standard 128 bytes,
// and is more VSG-idiomatic.
constexpr const char* FACE_VERT_SHADER = R"(
#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(push_constant) uniform PushConstants
{
    mat4 projection;
    mat4 modelView;
};

layout(location = 0) in vec3 vertex;
layout(location = 1) in vec3 normal;
layout(location = 2) in vec3 inColor;
layout(location = 0) out vec3 viewNormal;
layout(location = 1) out vec3 partColor;

out gl_PerVertex
{
    vec4 gl_Position;
};

void main()
{
    vec4 viewVertex = modelView * vec4(vertex, 1.0);
    viewNormal = mat3(modelView) * normal;
    partColor = inColor;
    gl_Position = projection * viewVertex;
}
)";

constexpr const char* FACE_FRAG_SHADER = R"(
#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(location = 0) in vec3 viewNormal;
layout(location = 1) in vec3 partColor;
layout(location = 0) out vec4 fragmentColor;

void main()
{
    vec3 normal = normalize(gl_FrontFacing ? viewNormal : -viewNormal);
    vec3 lightDirection = normalize(vec3(0.35, 0.55, 1.0));
    float diffuse = max(dot(normal, lightDirection), 0.0);
    vec3 shadedColor = partColor * (0.24 + 0.76 * diffuse);
    fragmentColor = vec4(shadedColor, 1.0);
}
)";

constexpr const char* LINE_VERT_SHADER = R"(
#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(push_constant) uniform PushConstants
{
    mat4 projection;
    mat4 modelView;
};

layout(location = 0) in vec3 vertex;
layout(location = 1) in vec3 inColor;
layout(location = 0) out vec3 lineColor;

out gl_PerVertex
{
    vec4 gl_Position;
};

void main()
{
    vec4 viewVertex = modelView * vec4(vertex, 1.0);
    lineColor = inColor;
    gl_Position = projection * viewVertex;
}
)";

constexpr const char* LINE_FRAG_SHADER = R"(
#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(location = 0) in vec3 lineColor;
layout(location = 0) out vec4 fragmentColor;

void main()
{
    fragmentColor = vec4(lineColor, 1.0);
}
)";

constexpr const char* POINT_VERT_SHADER = R"(
#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(push_constant) uniform PushConstants
{
    mat4 projection;
    mat4 modelView;
};

layout(location = 0) in vec3 vertex;
layout(location = 1) in vec3 inColor;
layout(location = 0) out vec3 pointColor;

out gl_PerVertex
{
    vec4 gl_Position;
    float gl_PointSize;
};

void main()
{
    vec4 viewVertex = modelView * vec4(vertex, 1.0);
    pointColor = inColor;
    gl_Position = projection * viewVertex;
    gl_PointSize = 7.0;
}
)";

constexpr const char* POINT_FRAG_SHADER = R"(
#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(location = 0) in vec3 pointColor;
layout(location = 0) out vec4 fragmentColor;

void main()
{
    vec2 centered = gl_PointCoord * 2.0 - 1.0;
    if (dot(centered, centered) > 1.0)
    {
        discard;
    }

    fragmentColor = vec4(pointColor, 1.0);
}
)";

vsg::ref_ptr<vsg::BindGraphicsPipeline> createPrimitivePipeline(
    const char* vertexShaderSource,
    const char* fragmentShaderSource,
    VkPrimitiveTopology topology,
    bool includeNormals,
    bool includeColors,
    bool depthWrite)
{
    vsg::DescriptorSetLayouts descriptorSetLayouts;
    vsg::PushConstantRanges pushConstantRanges{
        {VK_SHADER_STAGE_VERTEX_BIT, 0, 128}};
    auto pipelineLayout = vsg::PipelineLayout::create(descriptorSetLayouts, pushConstantRanges);

    auto vertexShaderHints = vsg::ShaderCompileSettings::create();
    auto vertexShader = vsg::ShaderStage::create(
        VK_SHADER_STAGE_VERTEX_BIT, "main", vertexShaderSource, vertexShaderHints);
    auto fragmentShaderHints = vsg::ShaderCompileSettings::create();
    auto fragmentShader = vsg::ShaderStage::create(
        VK_SHADER_STAGE_FRAGMENT_BIT, "main", fragmentShaderSource, fragmentShaderHints);
    auto shaderStages = vsg::ShaderStages{vertexShader, fragmentShader};

    auto vertexInputState = vsg::VertexInputState::create();
    auto& bindings = vertexInputState->vertexBindingDescriptions;
    auto& attributes = vertexInputState->vertexAttributeDescriptions;

    constexpr uint32_t offset = 0;
    bindings.emplace_back(VkVertexInputBindingDescription{0, sizeof(vsg::vec3), VK_VERTEX_INPUT_RATE_VERTEX});
    attributes.emplace_back(VkVertexInputAttributeDescription{0, 0, VK_FORMAT_R32G32B32_SFLOAT, offset});

    if (includeNormals)
    {
        bindings.emplace_back(VkVertexInputBindingDescription{1, sizeof(vsg::vec3), VK_VERTEX_INPUT_RATE_VERTEX});
        attributes.emplace_back(VkVertexInputAttributeDescription{1, 1, VK_FORMAT_R32G32B32_SFLOAT, offset});
    }

    if (includeColors)
    {
        uint32_t colorBinding = includeNormals ? 2u : 1u;
        bindings.emplace_back(VkVertexInputBindingDescription{colorBinding, sizeof(vsg::vec3), VK_VERTEX_INPUT_RATE_VERTEX});
        attributes.emplace_back(VkVertexInputAttributeDescription{2, colorBinding, VK_FORMAT_R32G32B32_SFLOAT, offset});
    }

    auto inputAssemblyState = vsg::InputAssemblyState::create();
    inputAssemblyState->topology = topology;

    auto rasterizationState = vsg::RasterizationState::create();
    rasterizationState->cullMode = VK_CULL_MODE_NONE;
    rasterizationState->lineWidth = 1.0f;

    auto depthStencilState = vsg::DepthStencilState::create();
    depthStencilState->depthTestEnable = VK_TRUE;
    depthStencilState->depthWriteEnable = depthWrite ? VK_TRUE : VK_FALSE;
    depthStencilState->depthCompareOp = VK_COMPARE_OP_GREATER_OR_EQUAL;

    auto graphicsPipelineStates = vsg::GraphicsPipelineStates{
        vertexInputState,
        inputAssemblyState,
        rasterizationState,
        vsg::ColorBlendState::create(),
        vsg::MultisampleState::create(),
        depthStencilState};

    auto graphicsPipeline = vsg::GraphicsPipeline::create(pipelineLayout, shaderStages, graphicsPipelineStates);
    return vsg::BindGraphicsPipeline::create(graphicsPipeline);
}

void tagNode(const vsg::ref_ptr<vsg::Node>& node,
             uint32_t partId,
             selection::PrimitiveKind primitiveKind)
{
    if (!node)
    {
        return;
    }

    node->setValue(PART_ID_KEY, partId);
    node->setValue(PRIMITIVE_KIND_KEY, static_cast<uint32_t>(primitiveKind));
}

struct PrimitiveNodeBuild
{
    vsg::ref_ptr<vsg::Node> node;
    vsg::ref_ptr<vsg::vec3Array> colors;
};

PrimitiveNodeBuild createPositionOnlyNode(
    const std::vector<vsg::vec3>& positions,
    const vsg::ref_ptr<vsg::BindGraphicsPipeline>& pipeline,
    const vsg::vec3& color,
    uint32_t partId,
    selection::PrimitiveKind primitiveKind)
{
    auto stateGroup = vsg::StateGroup::create();
    tagNode(stateGroup, partId, primitiveKind);

    if (positions.empty())
    {
        return {stateGroup, {}};
    }

    auto positionArray = vsg::vec3Array::create(static_cast<uint32_t>(positions.size()));
    auto colors = vsg::vec3Array::create(static_cast<uint32_t>(positions.size()));
    colors->properties.dataVariance = vsg::DYNAMIC_DATA;
    auto indices = vsg::uintArray::create(static_cast<uint32_t>(positions.size()));

    for (std::size_t index = 0; index < positions.size(); ++index)
    {
        (*positionArray)[static_cast<uint32_t>(index)] = positions[index];
        (*colors)[static_cast<uint32_t>(index)] = color;
        (*indices)[static_cast<uint32_t>(index)] = static_cast<uint32_t>(index);
    }

    auto drawCommands = vsg::VertexIndexDraw::create();
    drawCommands->assignArrays(vsg::DataList{positionArray, colors});
    drawCommands->assignIndices(indices);
    drawCommands->indexCount = indices->width();
    drawCommands->instanceCount = 1;

    stateGroup->add(pipeline);
    stateGroup->addChild(drawCommands);
    return {stateGroup, colors};
}

struct FaceNodeBuild
{
    vsg::ref_ptr<vsg::Node> node;
    vsg::ref_ptr<vsg::vec3Array> colors;
};

FaceNodeBuild createFaceNode(
    const std::vector<vsg::vec3>& facePositions,
    const std::vector<vsg::vec3>& faceNormals,
    const vsg::vec3& color,
    uint32_t partId)
{
    auto stateGroup = vsg::StateGroup::create();
    tagNode(stateGroup, partId, selection::PrimitiveKind::Face);

    if (facePositions.empty())
    {
        return {stateGroup, {}};
    }

    auto vertexCount = static_cast<uint32_t>(facePositions.size());
    auto positions = vsg::vec3Array::create(vertexCount);
    auto normals = vsg::vec3Array::create(vertexCount);
    auto colors = vsg::vec3Array::create(vertexCount);
    colors->properties.dataVariance = vsg::DYNAMIC_DATA;
    auto indices = vsg::uintArray::create(vertexCount);

    for (std::size_t index = 0; index < facePositions.size(); ++index)
    {
        auto i = static_cast<uint32_t>(index);
        (*positions)[i] = facePositions[index];
        (*normals)[i] = faceNormals[index];
        (*colors)[i] = color;
        (*indices)[i] = i;
    }

    auto drawCommands = vsg::VertexIndexDraw::create();
    drawCommands->assignArrays(vsg::DataList{positions, normals, colors});
    drawCommands->assignIndices(indices);
    drawCommands->indexCount = indices->width();
    drawCommands->instanceCount = 1;

    auto facePipeline = createPrimitivePipeline(
        FACE_VERT_SHADER, FACE_FRAG_SHADER,
        VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
        true, true, true);

    stateGroup->add(facePipeline);
    stateGroup->addChild(drawCommands);
    return {stateGroup, colors};
}

void applyColor(const vsg::ref_ptr<vsg::vec3Array>& colors, const vsg::vec3& color)
{
    if (!colors)
    {
        return;
    }

    for (auto& currentColor : *colors)
    {
        currentColor = color;
    }
    colors->dirty();
}

void applyColorRange(const vsg::ref_ptr<vsg::vec3Array>& colors,
                     uint32_t firstIndex,
                     uint32_t count,
                     const vsg::vec3& color)
{
    if (!colors || count == 0u || firstIndex >= colors->size())
    {
        return;
    }

    const uint32_t endIndex = std::min<uint32_t>(static_cast<uint32_t>(colors->size()), firstIndex + count);
    for (uint32_t index = firstIndex; index < endIndex; ++index)
    {
        (*colors)[index] = color;
    }
    colors->dirty();
}

void restoreBaseColors(PartSceneNode& part)
{
    applyColor(part.faceColors, part.baseColor);
    applyColor(part.lineColors, BASE_EDGE_COLOR);
    applyColor(part.pointColors, BASE_VERTEX_COLOR);
}

bool applyFaceHighlight(PartSceneNode& part, uint32_t faceId)
{
    bool highlighted = false;
    for (const auto& span : part.faceSpans)
    {
        if (span.faceId != faceId)
        {
            continue;
        }

        applyColorRange(part.faceColors, span.firstTriangle * 3u, span.triangleCount * 3u, SELECTED_FACE_COLOR);
        highlighted = true;
    }
    return highlighted;
}

bool applyEdgeHighlight(PartSceneNode& part, uint32_t edgeId)
{
    bool highlighted = false;
    for (const auto& span : part.lineSpans)
    {
        if (span.edgeId != edgeId)
        {
            continue;
        }

        applyColorRange(part.lineColors, span.firstSegment * 2u, span.segmentCount * 2u, SELECTED_EDGE_COLOR);
        highlighted = true;
    }
    return highlighted;
}

bool applyVertexHighlight(PartSceneNode& part, uint32_t vertexId)
{
    bool highlighted = false;
    for (const auto& span : part.pointSpans)
    {
        if (span.vertexId != vertexId)
        {
            continue;
        }

        applyColorRange(part.pointColors, span.firstPoint, span.pointCount, SELECTED_VERTEX_COLOR);
        highlighted = true;
    }
    return highlighted;
}

bool sameSelection(const selection::SelectionToken& lhs, const selection::SelectionToken& rhs)
{
    return lhs.partId == rhs.partId &&
           lhs.kind == rhs.kind &&
           lhs.primitiveId == rhs.primitiveId;
}

bool applySelectionHighlight(PartSceneNode& part, const selection::SelectionToken& token)
{
    switch (token.kind)
    {
    case selection::PrimitiveKind::Part:
        applyColor(part.faceColors, SELECTED_PART_COLOR);
        return true;
    case selection::PrimitiveKind::Face:
        return applyFaceHighlight(part, token.primitiveId);
    case selection::PrimitiveKind::Edge:
        return applyEdgeHighlight(part, token.primitiveId);
    case selection::PrimitiveKind::Vertex:
        return applyVertexHighlight(part, token.primitiveId);
    case selection::PrimitiveKind::None:
    default:
        return false;
    }
}

struct BoundsAccumulator
{
    vsg::dvec3 min{std::numeric_limits<double>::max(),
                   std::numeric_limits<double>::max(),
                   std::numeric_limits<double>::max()};
    vsg::dvec3 max{std::numeric_limits<double>::lowest(),
                   std::numeric_limits<double>::lowest(),
                   std::numeric_limits<double>::lowest()};
    bool valid = false;

    void expand(const vsg::dvec3& bmin, const vsg::dvec3& bmax)
    {
        min.x = std::min(min.x, bmin.x);
        min.y = std::min(min.y, bmin.y);
        min.z = std::min(min.z, bmin.z);
        max.x = std::max(max.x, bmax.x);
        max.y = std::max(max.y, bmax.y);
        max.z = std::max(max.z, bmax.z);
        valid = true;
    }
};

vsg::vec3 resolveColor(const cad::ShapeNodeColor& color)
{
    return vsg::vec3(color.r, color.g, color.b);
}

void buildNodeSubgraph(
    const cad::ShapeNode& shapeNode,
    const vsg::ref_ptr<vsg::Group>& parentGroup,
    const TopLoc_Location& accumulatedLocation,
    std::vector<PartSceneNode>& parts,
    BoundsAccumulator& bounds,
    const mesh::MeshOptions& meshOptions,
    std::size_t& totalTriangles,
    std::size_t& totalLines,
    std::size_t& totalPoints,
    uint32_t& nextPartId)
{
    TopLoc_Location currentLocation = accumulatedLocation * shapeNode.location;

    if (shapeNode.type == cad::ShapeNodeType::Assembly)
    {
        auto group = vsg::Group::create();
        for (const auto& child : shapeNode.children)
        {
            buildNodeSubgraph(child, group, currentLocation, parts, bounds,
                              meshOptions, totalTriangles, totalLines, totalPoints, nextPartId);
        }
        parentGroup->addChild(group);
        return;
    }

    const uint32_t partId = nextPartId++;
    TopoDS_Shape locatedShape = shapeNode.shape.Located(currentLocation);
    auto meshResult = mesh::triangulate(locatedShape, meshOptions);

    auto color = resolveColor(shapeNode.color);
    auto faceNode = createFaceNode(meshResult.facePositions, meshResult.faceNormals, color, partId);
    auto lineNode = createPositionOnlyNode(
        meshResult.linePositions,
        createPrimitivePipeline(LINE_VERT_SHADER, LINE_FRAG_SHADER,
                               VK_PRIMITIVE_TOPOLOGY_LINE_LIST, false, true, false),
        BASE_EDGE_COLOR,
        partId,
        selection::PrimitiveKind::Edge);
    auto pointNode = createPositionOnlyNode(
        meshResult.pointPositions,
        createPrimitivePipeline(POINT_VERT_SHADER, POINT_FRAG_SHADER,
                               VK_PRIMITIVE_TOPOLOGY_POINT_LIST, false, true, false),
        BASE_VERTEX_COLOR,
        partId,
        selection::PrimitiveKind::Vertex);

    auto partGroup = vsg::Group::create();
    partGroup->setValue(PART_ID_KEY, partId);
    partGroup->addChild(faceNode.node);
    partGroup->addChild(lineNode.node);
    partGroup->addChild(pointNode.node);

    auto partSwitch = vsg::Switch::create();
    partSwitch->setValue(PART_ID_KEY, partId);
    partSwitch->addChild(true, partGroup);
    parentGroup->addChild(partSwitch);

    PartSceneNode partSceneNode;
    partSceneNode.partId = partId;
    partSceneNode.name = shapeNode.name;
    partSceneNode.switchNode = partSwitch;
    partSceneNode.baseColor = color;
    partSceneNode.faceColors = faceNode.colors;
    partSceneNode.lineColors = lineNode.colors;
    partSceneNode.pointColors = pointNode.colors;
    partSceneNode.pointSpans = std::move(meshResult.pointSpans);
    partSceneNode.lineSpans = std::move(meshResult.lineSpans);
    partSceneNode.faceSpans = std::move(meshResult.faceSpans);
    parts.push_back(std::move(partSceneNode));

    totalTriangles += meshResult.triangleCount;
    totalLines += meshResult.lineSegmentCount;
    totalPoints += meshResult.pointCount;

    if (meshResult.hasGeometry())
    {
        bounds.expand(meshResult.boundsMin, meshResult.boundsMax);
    }
}
} // namespace

AssemblySceneData buildAssemblyScene(
    const cad::AssemblyData& assembly,
    const mesh::MeshOptions& meshOptions,
    const SceneOptions& /*sceneOptions*/)
{
    auto root = vsg::Group::create();
    std::vector<PartSceneNode> parts;
    BoundsAccumulator bounds;
    std::size_t totalTriangles = 0;
    std::size_t totalLines = 0;
    std::size_t totalPoints = 0;
    uint32_t nextPartId = 0;

    TopLoc_Location identity;
    for (const auto& rootNode : assembly.roots)
    {
        buildNodeSubgraph(rootNode, root, identity, parts, bounds,
                          meshOptions, totalTriangles, totalLines, totalPoints, nextPartId);
    }

    AssemblySceneData sceneData;
    sceneData.scene = root;
    sceneData.parts = std::move(parts);

    if (bounds.valid)
    {
        sceneData.center = (bounds.min + bounds.max) * 0.5;
        sceneData.radius = vsg::length(bounds.max - bounds.min) * 0.5;
        sceneData.radius = std::max(sceneData.radius, 1.0);
    }

    sceneData.totalTriangleCount = totalTriangles;
    sceneData.totalLineSegmentCount = totalLines;
    sceneData.totalPointCount = totalPoints;

    return sceneData;
}

PartSceneNode* findPart(AssemblySceneData& sceneData, uint32_t partId)
{
    return const_cast<PartSceneNode*>(findPart(std::as_const(sceneData), partId));
}

const PartSceneNode* findPart(const AssemblySceneData& sceneData, uint32_t partId)
{
    if (partId == InvalidPartId)
    {
        return nullptr;
    }

    if (partId < sceneData.parts.size() && sceneData.parts[partId].partId == partId)
    {
        return &sceneData.parts[partId];
    }

    const auto itr = std::find_if(
        sceneData.parts.begin(),
        sceneData.parts.end(),
        [partId](const PartSceneNode& part)
        {
            return part.partId == partId;
        });
    return itr != sceneData.parts.end() ? &(*itr) : nullptr;
}

bool setSelectedPart(AssemblySceneData& sceneData, uint32_t partId)
{
    selection::SelectionToken token;
    token.partId = partId;
    token.kind = selection::PrimitiveKind::Part;
    token.primitiveId = partId;
    return setSelection(sceneData, token);
}

void clearSelectedPart(AssemblySceneData& sceneData)
{
    clearSelection(sceneData);
}

bool setSelection(AssemblySceneData& sceneData, const selection::SelectionToken& token)
{
    if (!token)
    {
        return false;
    }

    PartSceneNode* part = findPart(sceneData, token.partId);
    if (!part)
    {
        return false;
    }

    if (sameSelection(sceneData.selectedToken, token))
    {
        return true;
    }

    clearSelection(sceneData);
    if (!applySelectionHighlight(*part, token))
    {
        return false;
    }

    sceneData.selectedPartId = token.partId;
    sceneData.selectedToken = token;
    return true;
}

void clearSelection(AssemblySceneData& sceneData)
{
    if (sceneData.selectedPartId == InvalidPartId)
    {
        sceneData.selectedToken = {};
        return;
    }

    if (PartSceneNode* part = findPart(sceneData, sceneData.selectedPartId))
    {
        restoreBaseColors(*part);
    }

    sceneData.selectedPartId = InvalidPartId;
    sceneData.selectedToken = {};
}
} // namespace vsgocct::scene
