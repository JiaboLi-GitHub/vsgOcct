#include <vsgocct/scene/SceneBuilder.h>

#include <vsg/all.h>

#include <algorithm>
#include <cstddef>
#include <vector>

namespace vsgocct::scene
{
namespace
{
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
layout(location = 0) out vec3 viewNormal;

out gl_PerVertex
{
    vec4 gl_Position;
};

void main()
{
    vec4 viewVertex = modelView * vec4(vertex, 1.0);
    viewNormal = mat3(modelView) * normal;
    gl_Position = projection * viewVertex;
}
)";

constexpr const char* FACE_FRAG_SHADER = R"(
#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(location = 0) in vec3 viewNormal;
layout(location = 0) out vec4 fragmentColor;

void main()
{
    vec3 normal = normalize(gl_FrontFacing ? viewNormal : -viewNormal);
    vec3 lightDirection = normalize(vec3(0.35, 0.55, 1.0));
    float diffuse = max(dot(normal, lightDirection), 0.0);
    vec3 baseColor = vec3(0.74, 0.79, 0.86);
    vec3 shadedColor = baseColor * (0.24 + 0.76 * diffuse);
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

out gl_PerVertex
{
    vec4 gl_Position;
};

void main()
{
    vec4 viewVertex = modelView * vec4(vertex, 1.0);
    gl_Position = projection * viewVertex;
}
)";

constexpr const char* LINE_FRAG_SHADER = R"(
#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(location = 0) out vec4 fragmentColor;

void main()
{
    fragmentColor = vec4(vec3(0.12, 0.25, 0.40), 1.0);
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

out gl_PerVertex
{
    vec4 gl_Position;
    float gl_PointSize;
};

void main()
{
    vec4 viewVertex = modelView * vec4(vertex, 1.0);
    gl_Position = projection * viewVertex;
    gl_PointSize = 7.0;
}
)";

constexpr const char* POINT_FRAG_SHADER = R"(
#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(location = 0) out vec4 fragmentColor;

void main()
{
    vec2 centered = gl_PointCoord * 2.0 - 1.0;
    if (dot(centered, centered) > 1.0)
    {
        discard;
    }

    fragmentColor = vec4(vec3(0.90, 0.40, 0.12), 1.0);
}
)";

vsg::ref_ptr<vsg::BindGraphicsPipeline> createPrimitivePipeline(
    const char* vertexShaderSource,
    const char* fragmentShaderSource,
    VkPrimitiveTopology topology,
    bool includeNormals,
    bool depthWrite)
{
    vsg::DescriptorSetLayouts descriptorSetLayouts;
    vsg::PushConstantRanges pushConstantRanges{{VK_SHADER_STAGE_VERTEX_BIT, 0, 128}};
    auto pipelineLayout = vsg::PipelineLayout::create(descriptorSetLayouts, pushConstantRanges);

    auto vertexShaderHints = vsg::ShaderCompileSettings::create();
    auto vertexShader = vsg::ShaderStage::create(VK_SHADER_STAGE_VERTEX_BIT, "main", vertexShaderSource, vertexShaderHints);
    auto fragmentShaderHints = vsg::ShaderCompileSettings::create();
    auto fragmentShader =
        vsg::ShaderStage::create(VK_SHADER_STAGE_FRAGMENT_BIT, "main", fragmentShaderSource, fragmentShaderHints);
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

vsg::ref_ptr<vsg::Node> createPositionOnlyNode(
    const std::vector<vsg::vec3>& positions,
    const vsg::ref_ptr<vsg::BindGraphicsPipeline>& pipeline)
{
    if (positions.empty())
    {
        return vsg::Group::create();
    }

    auto positionArray = vsg::vec3Array::create(static_cast<uint32_t>(positions.size()));
    auto indices = vsg::uintArray::create(static_cast<uint32_t>(positions.size()));

    for (std::size_t index = 0; index < positions.size(); ++index)
    {
        (*positionArray)[static_cast<uint32_t>(index)] = positions[index];
        (*indices)[static_cast<uint32_t>(index)] = static_cast<uint32_t>(index);
    }

    auto drawCommands = vsg::VertexIndexDraw::create();
    drawCommands->assignArrays(vsg::DataList{positionArray});
    drawCommands->assignIndices(indices);
    drawCommands->indexCount = indices->width();
    drawCommands->instanceCount = 1;

    auto stateGroup = vsg::StateGroup::create();
    stateGroup->add(pipeline);
    stateGroup->addChild(drawCommands);
    return stateGroup;
}

vsg::ref_ptr<vsg::Node> createFaceNode(
    const std::vector<vsg::vec3>& facePositions,
    const std::vector<vsg::vec3>& faceNormals)
{
    if (facePositions.empty())
    {
        return vsg::Group::create();
    }

    auto positions = vsg::vec3Array::create(static_cast<uint32_t>(facePositions.size()));
    auto normals = vsg::vec3Array::create(static_cast<uint32_t>(faceNormals.size()));
    auto indices = vsg::uintArray::create(static_cast<uint32_t>(facePositions.size()));

    for (std::size_t index = 0; index < facePositions.size(); ++index)
    {
        (*positions)[static_cast<uint32_t>(index)] = facePositions[index];
        (*normals)[static_cast<uint32_t>(index)] = faceNormals[index];
        (*indices)[static_cast<uint32_t>(index)] = static_cast<uint32_t>(index);
    }

    auto drawCommands = vsg::VertexIndexDraw::create();
    drawCommands->assignArrays(vsg::DataList{positions, normals});
    drawCommands->assignIndices(indices);
    drawCommands->indexCount = indices->width();
    drawCommands->instanceCount = 1;

    auto stateGroup = vsg::StateGroup::create();
    stateGroup->add(createPrimitivePipeline(
        FACE_VERT_SHADER,
        FACE_FRAG_SHADER,
        VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
        true,
        true));
    stateGroup->addChild(drawCommands);
    return stateGroup;
}

vsg::ref_ptr<vsg::Switch> createPrimitiveSwitch(const vsg::ref_ptr<vsg::Node>& node, bool visible)
{
    auto primitiveSwitch = vsg::Switch::create();
    primitiveSwitch->addChild(visible, node ? node : vsg::Group::create());
    return primitiveSwitch;
}
} // namespace

StepSceneData buildScene(const mesh::MeshResult& meshResult, const SceneOptions& options)
{
    auto pointNode = createPositionOnlyNode(
        meshResult.pointPositions,
        createPrimitivePipeline(POINT_VERT_SHADER, POINT_FRAG_SHADER, VK_PRIMITIVE_TOPOLOGY_POINT_LIST, false, false));
    auto lineNode = createPositionOnlyNode(
        meshResult.linePositions,
        createPrimitivePipeline(LINE_VERT_SHADER, LINE_FRAG_SHADER, VK_PRIMITIVE_TOPOLOGY_LINE_LIST, false, false));
    auto faceNode = createFaceNode(meshResult.facePositions, meshResult.faceNormals);

    auto pointSwitch = createPrimitiveSwitch(pointNode, options.pointsVisible);
    auto lineSwitch = createPrimitiveSwitch(lineNode, options.linesVisible);
    auto faceSwitch = createPrimitiveSwitch(faceNode, options.facesVisible);

    auto root = vsg::Group::create();
    root->addChild(faceSwitch);
    root->addChild(lineSwitch);
    root->addChild(pointSwitch);

    StepSceneData sceneData;
    sceneData.scene = root;
    sceneData.pointSwitch = pointSwitch;
    sceneData.lineSwitch = lineSwitch;
    sceneData.faceSwitch = faceSwitch;
    sceneData.center = (meshResult.boundsMin + meshResult.boundsMax) * 0.5;
    sceneData.radius = vsg::length(meshResult.boundsMax - meshResult.boundsMin) * 0.5;
    sceneData.radius = std::max(sceneData.radius, 1.0);
    sceneData.pointCount = meshResult.pointCount;
    sceneData.lineSegmentCount = meshResult.lineSegmentCount;
    sceneData.triangleCount = meshResult.triangleCount;
    return sceneData;
}
} // namespace vsgocct::scene
