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
const vsg::vec3 HOVER_PART_COLOR = vsg::vec3(0.62f, 0.92f, 0.22f);
const vsg::vec3 HOVER_FACE_COLOR = vsg::vec3(0.42f, 0.96f, 0.34f);
const vsg::vec3 HOVER_EDGE_COLOR = vsg::vec3(1.0f, 0.86f, 0.12f);
const vsg::vec3 HOVER_VERTEX_COLOR = vsg::vec3(0.38f, 0.84f, 1.0f);

struct HighlightPalette
{
    vsg::vec3 partColor;
    vsg::vec3 faceColor;
    vsg::vec3 edgeColor;
    vsg::vec3 vertexColor;
};

const HighlightPalette SELECTED_PALETTE{
    SELECTED_PART_COLOR,
    SELECTED_FACE_COLOR,
    SELECTED_EDGE_COLOR,
    SELECTED_VERTEX_COLOR};
const HighlightPalette HOVER_PALETTE{
    HOVER_PART_COLOR,
    HOVER_FACE_COLOR,
    HOVER_EDGE_COLOR,
    HOVER_VERTEX_COLOR};

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
layout(location = 2) in vec4 inColor;
layout(location = 0) out vec3 viewNormal;
layout(location = 1) out vec4 partColor;

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
layout(location = 1) in vec4 partColor;
layout(location = 0) out vec4 fragmentColor;

void main()
{
    vec3 normal = normalize(gl_FrontFacing ? viewNormal : -viewNormal);
    vec3 lightDirection = normalize(vec3(0.35, 0.55, 1.0));
    float diffuse = max(dot(normal, lightDirection), 0.0);
    vec3 shadedColor = partColor.rgb * (0.24 + 0.76 * diffuse);
    fragmentColor = vec4(shadedColor, partColor.a);
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
    VkFormat colorFormat,
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

    if (colorFormat != VK_FORMAT_UNDEFINED)
    {
        const uint32_t colorBinding = includeNormals ? 2u : 1u;
        const uint32_t colorStride = colorFormat == VK_FORMAT_R32G32B32A32_SFLOAT
                                         ? sizeof(vsg::vec4)
                                         : sizeof(vsg::vec3);
        bindings.emplace_back(VkVertexInputBindingDescription{
            colorBinding,
            colorStride,
            VK_VERTEX_INPUT_RATE_VERTEX});
        attributes.emplace_back(VkVertexInputAttributeDescription{2, colorBinding, colorFormat, offset});
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
    vsg::ref_ptr<vsg::vec4Array> colors;
    vsg::ref_ptr<vsg::PbrMaterialValue> materialValue;
};

vsg::vec4 resolveBaseColor(const cad::ShapeVisualMaterial& material)
{
    return vsg::vec4(
        material.baseColorFactor[0],
        material.baseColorFactor[1],
        material.baseColorFactor[2],
        material.baseColorFactor[3]);
}

vsg::PbrMaterial buildPbrMaterial(const cad::ShapeVisualMaterial& material)
{
    vsg::PbrMaterial pbrMaterial;
    pbrMaterial.baseColorFactor = vsg::vec4(1.0f, 1.0f, 1.0f, 1.0f);
    pbrMaterial.emissiveFactor = vsg::vec4(
        material.emissiveFactor[0],
        material.emissiveFactor[1],
        material.emissiveFactor[2],
        1.0f);
    pbrMaterial.metallicFactor = material.metallicFactor;
    pbrMaterial.roughnessFactor = material.roughnessFactor;
    pbrMaterial.alphaMask = material.alphaMask ? 1.0f : 0.0f;
    pbrMaterial.alphaMaskCutoff = material.alphaCutoff;
    return pbrMaterial;
}

cad::ShapeVisualMaterial buildPresetMaterial(
    float red,
    float green,
    float blue,
    float alpha,
    float metallic,
    float roughness,
    bool doubleSided)
{
    cad::ShapeVisualMaterial material;
    material.baseColorFactor = {red, green, blue, alpha};
    material.emissiveFactor = {0.0f, 0.0f, 0.0f};
    material.metallicFactor = metallic;
    material.roughnessFactor = roughness;
    material.alphaMask = false;
    material.alphaCutoff = 0.5f;
    material.doubleSided = doubleSided;
    material.hasPbr = true;
    material.source = cad::ShapeVisualMaterialSource::Pbr;
    return material;
}

FaceNodeBuild createLegacyFaceNode(
    const std::vector<vsg::vec3>& facePositions,
    const std::vector<vsg::vec3>& faceNormals,
    const vsg::vec4& color,
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
    auto colors = vsg::vec4Array::create(vertexCount);
    colors->properties.dataVariance = vsg::DYNAMIC_DATA;
    auto indices = vsg::uintArray::create(vertexCount);

    for (std::size_t index = 0; index < facePositions.size(); ++index)
    {
        const auto i = static_cast<uint32_t>(index);
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
        FACE_VERT_SHADER,
        FACE_FRAG_SHADER,
        VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
        true,
        VK_FORMAT_R32G32B32A32_SFLOAT,
        true);

    stateGroup->add(facePipeline);
    stateGroup->addChild(drawCommands);
    return {stateGroup, colors, {}};
}

FaceNodeBuild createPbrFaceNode(
    const std::vector<vsg::vec3>& facePositions,
    const std::vector<vsg::vec3>& faceNormals,
    const cad::ShapeVisualMaterial& material,
    const vsg::vec4& vertexColor,
    uint32_t partId)
{
    auto stateGroup = vsg::StateGroup::create();
    tagNode(stateGroup, partId, selection::PrimitiveKind::Face);

    if (facePositions.empty())
    {
        return {stateGroup, {}};
    }

    const auto vertexCount = static_cast<uint32_t>(facePositions.size());
    auto positions = vsg::vec3Array::create(vertexCount);
    auto normals = vsg::vec3Array::create(vertexCount);
    auto colors = vsg::vec4Array::create(vertexCount);
    colors->properties.dataVariance = vsg::DYNAMIC_DATA;
    auto indices = vsg::uintArray::create(vertexCount);

    for (std::size_t index = 0; index < facePositions.size(); ++index)
    {
        const auto i = static_cast<uint32_t>(index);
        (*positions)[i] = facePositions[index];
        (*normals)[i] = faceNormals[index];
        (*colors)[i] = vertexColor;
        (*indices)[i] = i;
    }

    vsg::DataList arrays;
    auto shaderSet = vsg::createPhysicsBasedRenderingShaderSet();
    auto pipelineConfig = vsg::GraphicsPipelineConfigurator::create(shaderSet);
    pipelineConfig->assignArray(arrays, "vsg_Vertex", VK_VERTEX_INPUT_RATE_VERTEX, positions);
    pipelineConfig->assignArray(arrays, "vsg_Normal", VK_VERTEX_INPUT_RATE_VERTEX, normals);
    pipelineConfig->assignArray(arrays, "vsg_Color", VK_VERTEX_INPUT_RATE_VERTEX, colors);

    auto materialValue = vsg::PbrMaterialValue::create();
    materialValue->value() = buildPbrMaterial(material);
    auto texCoordIndices = vsg::TexCoordIndicesValue::create();

    pipelineConfig->assignDescriptor("material", materialValue);
    pipelineConfig->assignDescriptor("texCoordIndices", texCoordIndices);

    if (pipelineConfig->descriptorConfigurator)
    {
        pipelineConfig->descriptorConfigurator->two_sided = material.doubleSided;
        pipelineConfig->descriptorConfigurator->blending =
            vertexColor.a < 0.999f && !material.alphaMask;
    }

    pipelineConfig->init();
    pipelineConfig->copyTo(stateGroup);

    auto drawCommands = vsg::VertexIndexDraw::create();
    drawCommands->assignArrays(arrays);
    drawCommands->assignIndices(indices);
    drawCommands->indexCount = indices->width();
    drawCommands->instanceCount = 1;
    stateGroup->addChild(drawCommands);

    return {stateGroup, colors, materialValue};
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

void applyColor(const vsg::ref_ptr<vsg::vec4Array>& colors, const vsg::vec4& color)
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

void applyColorRange(const vsg::ref_ptr<vsg::vec4Array>& colors,
                     uint32_t firstIndex,
                     uint32_t count,
                     const vsg::vec4& color)
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

vsg::vec4 faceHighlightColor(const PartSceneNode& part, const vsg::vec3& color)
{
    return vsg::vec4(color.r, color.g, color.b, part.baseColor.a);
}

void restoreBaseColors(PartSceneNode& part)
{
    applyColor(part.faceColors, part.baseColor);
    applyColor(part.lineColors, BASE_EDGE_COLOR);
    applyColor(part.pointColors, BASE_VERTEX_COLOR);
}

bool applyFaceHighlight(PartSceneNode& part, uint32_t faceId, const vsg::vec3& color)
{
    bool highlighted = false;
    const auto highlightColor = faceHighlightColor(part, color);
    for (const auto& span : part.faceSpans)
    {
        if (span.faceId != faceId)
        {
            continue;
        }

        applyColorRange(part.faceColors, span.firstTriangle * 3u, span.triangleCount * 3u, highlightColor);
        highlighted = true;
    }
    return highlighted;
}

bool applyEdgeHighlight(PartSceneNode& part, uint32_t edgeId, const vsg::vec3& color)
{
    bool highlighted = false;
    for (const auto& span : part.lineSpans)
    {
        if (span.edgeId != edgeId)
        {
            continue;
        }

        applyColorRange(part.lineColors, span.firstSegment * 2u, span.segmentCount * 2u, color);
        highlighted = true;
    }
    return highlighted;
}

bool applyVertexHighlight(PartSceneNode& part, uint32_t vertexId, const vsg::vec3& color)
{
    bool highlighted = false;
    for (const auto& span : part.pointSpans)
    {
        if (span.vertexId != vertexId)
        {
            continue;
        }

        applyColorRange(part.pointColors, span.firstPoint, span.pointCount, color);
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

bool applySelectionHighlight(PartSceneNode& part,
                             const selection::SelectionToken& token,
                             const HighlightPalette& palette)
{
    switch (token.kind)
    {
    case selection::PrimitiveKind::Part:
        applyColor(part.faceColors, faceHighlightColor(part, palette.partColor));
        return true;
    case selection::PrimitiveKind::Face:
        return applyFaceHighlight(part, token.primitiveId, palette.faceColor);
    case selection::PrimitiveKind::Edge:
        return applyEdgeHighlight(part, token.primitiveId, palette.edgeColor);
    case selection::PrimitiveKind::Vertex:
        return applyVertexHighlight(part, token.primitiveId, palette.vertexColor);
    case selection::PrimitiveKind::None:
    default:
        return false;
    }
}

void appendPartId(std::vector<uint32_t>& partIds, uint32_t partId)
{
    if (partId == InvalidPartId ||
        std::find(partIds.begin(), partIds.end(), partId) != partIds.end())
    {
        return;
    }

    partIds.push_back(partId);
}

std::vector<uint32_t> collectAffectedPartIds(const selection::SelectionToken& first,
                                             const selection::SelectionToken& second = {},
                                             const selection::SelectionToken& third = {})
{
    std::vector<uint32_t> partIds;
    if (first)
    {
        appendPartId(partIds, first.partId);
    }
    if (second)
    {
        appendPartId(partIds, second.partId);
    }
    if (third)
    {
        appendPartId(partIds, third.partId);
    }
    return partIds;
}

std::vector<uint32_t> collectAllPartIds(const AssemblySceneData& sceneData)
{
    std::vector<uint32_t> partIds;
    partIds.reserve(sceneData.parts.size());
    for (const auto& part : sceneData.parts)
    {
        partIds.push_back(part.partId);
    }
    return partIds;
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

void buildNodeSubgraph(
    const cad::ShapeNode& shapeNode,
    const vsg::ref_ptr<vsg::Group>& parentGroup,
    const TopLoc_Location& accumulatedLocation,
    std::vector<PartSceneNode>& parts,
    BoundsAccumulator& bounds,
    const mesh::MeshOptions& meshOptions,
    const SceneOptions& sceneOptions,
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
                              meshOptions, sceneOptions, totalTriangles, totalLines, totalPoints, nextPartId);
        }
        parentGroup->addChild(group);
        return;
    }

    const uint32_t partId = nextPartId++;
    TopoDS_Shape locatedShape = shapeNode.shape.Located(currentLocation);
    auto meshResult = mesh::triangulate(locatedShape, meshOptions);

    const auto faceColor = resolveBaseColor(shapeNode.visualMaterial);
    const auto faceNode = sceneOptions.shadingMode == ShadingMode::Pbr
                              ? createPbrFaceNode(
                                    meshResult.facePositions,
                                    meshResult.faceNormals,
                                    shapeNode.visualMaterial,
                                    faceColor,
                                    partId)
                              : createLegacyFaceNode(
                                    meshResult.facePositions,
                                    meshResult.faceNormals,
                                    faceColor,
                                    partId);
    auto lineNode = createPositionOnlyNode(
        meshResult.linePositions,
        createPrimitivePipeline(
            LINE_VERT_SHADER,
            LINE_FRAG_SHADER,
            VK_PRIMITIVE_TOPOLOGY_LINE_LIST,
            false,
            VK_FORMAT_R32G32B32_SFLOAT,
            false),
        BASE_EDGE_COLOR,
        partId,
        selection::PrimitiveKind::Edge);
    auto pointNode = createPositionOnlyNode(
        meshResult.pointPositions,
        createPrimitivePipeline(
            POINT_VERT_SHADER,
            POINT_FRAG_SHADER,
            VK_PRIMITIVE_TOPOLOGY_POINT_LIST,
            false,
            VK_FORMAT_R32G32B32_SFLOAT,
            false),
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
    partSceneNode.baseColor = faceColor;
    partSceneNode.importedMaterial = shapeNode.visualMaterial;
    partSceneNode.visualMaterial = shapeNode.visualMaterial;
    partSceneNode.pbrMaterialValue = faceNode.materialValue;
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
    const SceneOptions& sceneOptions)
{
    auto root = vsg::Group::create();
    if (sceneOptions.shadingMode == ShadingMode::Pbr && sceneOptions.addHeadlight)
    {
        root->addChild(vsg::createHeadlight());
    }

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
                          meshOptions, sceneOptions, totalTriangles, totalLines, totalPoints, nextPartId);
    }

    AssemblySceneData sceneData;
    sceneData.scene = root;
    sceneData.parts = std::move(parts);
    sceneData.shadingMode = sceneOptions.shadingMode;
    sceneData.materialPreset = MaterialPreset::Imported;

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

const char* materialPresetName(MaterialPreset preset)
{
    switch (preset)
    {
    case MaterialPreset::Imported:
        return "Imported";
    case MaterialPreset::Iron:
        return "Iron";
    case MaterialPreset::Copper:
        return "Copper";
    case MaterialPreset::Gold:
        return "Gold";
    case MaterialPreset::Wood:
        return "Wood";
    case MaterialPreset::Acrylic:
        return "Acrylic";
    default:
        return "Imported";
    }
}

cad::ShapeVisualMaterial makeMaterialPreset(MaterialPreset preset)
{
    switch (preset)
    {
    case MaterialPreset::Iron:
        return buildPresetMaterial(0.58f, 0.60f, 0.62f, 1.0f, 0.94f, 0.42f, false);
    case MaterialPreset::Copper:
        return buildPresetMaterial(0.95f, 0.64f, 0.54f, 1.0f, 1.0f, 0.28f, false);
    case MaterialPreset::Gold:
        return buildPresetMaterial(1.0f, 0.84f, 0.24f, 1.0f, 1.0f, 0.18f, false);
    case MaterialPreset::Wood:
        return buildPresetMaterial(0.56f, 0.36f, 0.20f, 1.0f, 0.0f, 0.82f, false);
    case MaterialPreset::Acrylic:
        return buildPresetMaterial(0.82f, 0.92f, 1.0f, 0.38f, 0.0f, 0.08f, true);
    case MaterialPreset::Imported:
    default:
        return {};
    }
}

void rebuildHighlights(AssemblySceneData& sceneData, const std::vector<uint32_t>& affectedPartIds)
{
    for (uint32_t partId : affectedPartIds)
    {
        if (PartSceneNode* part = findPart(sceneData, partId))
        {
            restoreBaseColors(*part);
        }
    }

    if (sceneData.selectedToken)
    {
        if (PartSceneNode* part = findPart(sceneData, sceneData.selectedToken.partId))
        {
            applySelectionHighlight(*part, sceneData.selectedToken, SELECTED_PALETTE);
        }
    }

    if (sceneData.hoverToken && !sameSelection(sceneData.hoverToken, sceneData.selectedToken))
    {
        if (PartSceneNode* part = findPart(sceneData, sceneData.hoverToken.partId))
        {
            applySelectionHighlight(*part, sceneData.hoverToken, HOVER_PALETTE);
        }
    }
}

bool applyMaterialPreset(AssemblySceneData& sceneData, MaterialPreset preset)
{
    if (sceneData.materialPreset == preset)
    {
        return false;
    }

    const auto affectedPartIds = collectAllPartIds(sceneData);
    for (auto& part : sceneData.parts)
    {
        part.visualMaterial = preset == MaterialPreset::Imported
                                  ? part.importedMaterial
                                  : makeMaterialPreset(preset);
        part.baseColor = resolveBaseColor(part.visualMaterial);

        if (part.pbrMaterialValue)
        {
            part.pbrMaterialValue->value() = buildPbrMaterial(part.visualMaterial);
            part.pbrMaterialValue->dirty();
        }
    }

    sceneData.materialPreset = preset;
    rebuildHighlights(sceneData, affectedPartIds);
    return true;
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

    std::vector<uint32_t> affectedPartIds = collectAffectedPartIds(
        sceneData.selectedToken,
        sceneData.hoverToken,
        token);

    if (!applySelectionHighlight(*part, token, SELECTED_PALETTE))
    {
        return false;
    }

    sceneData.selectedPartId = token.partId;
    sceneData.selectedToken = token;
    rebuildHighlights(sceneData, affectedPartIds);
    return true;
}

void clearSelection(AssemblySceneData& sceneData)
{
    if (!sceneData.selectedToken)
    {
        sceneData.selectedPartId = InvalidPartId;
        return;
    }

    std::vector<uint32_t> affectedPartIds = collectAffectedPartIds(
        sceneData.selectedToken,
        sceneData.hoverToken);
    sceneData.selectedPartId = InvalidPartId;
    sceneData.selectedToken = {};
    rebuildHighlights(sceneData, affectedPartIds);
}

bool setHoverSelection(AssemblySceneData& sceneData, const selection::SelectionToken& token)
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

    if (sameSelection(sceneData.hoverToken, token))
    {
        return true;
    }

    std::vector<uint32_t> affectedPartIds = collectAffectedPartIds(
        sceneData.selectedToken,
        sceneData.hoverToken,
        token);

    if (!applySelectionHighlight(*part, token, HOVER_PALETTE))
    {
        return false;
    }

    sceneData.hoverToken = token;
    rebuildHighlights(sceneData, affectedPartIds);
    return true;
}

void clearHoverSelection(AssemblySceneData& sceneData)
{
    if (!sceneData.hoverToken)
    {
        return;
    }

    std::vector<uint32_t> affectedPartIds = collectAffectedPartIds(
        sceneData.selectedToken,
        sceneData.hoverToken);
    sceneData.hoverToken = {};
    rebuildHighlights(sceneData, affectedPartIds);
}
} // namespace vsgocct::scene
