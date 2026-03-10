#include <vsgocct/StepModelLoader.h>

#include <BRepBndLib.hxx>
#include <BRepMesh_IncrementalMesh.hxx>
#include <BRep_Tool.hxx>
#include <Bnd_Box.hxx>
#include <IFSelect_ReturnStatus.hxx>
#include <Poly_Triangulation.hxx>
#include <STEPControl_Reader.hxx>
#include <TopAbs_Orientation.hxx>
#include <TopAbs_ShapeEnum.hxx>
#include <TopExp_Explorer.hxx>
#include <TopLoc_Location.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Face.hxx>
#include <gp_Pnt.hxx>
#include <gp_Trsf.hxx>
#include <gp_Vec.hxx>

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace vsgocct
{
namespace
{
constexpr const char* VERT_SHADER = R"(
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

constexpr const char* FRAG_SHADER = R"(
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

struct MeshBuffers
{
    std::vector<vsg::vec3> positions;
    std::vector<vsg::vec3> normals;
    vsg::dvec3 min = {
        std::numeric_limits<double>::max(),
        std::numeric_limits<double>::max(),
        std::numeric_limits<double>::max()};
    vsg::dvec3 max = {
        std::numeric_limits<double>::lowest(),
        std::numeric_limits<double>::lowest(),
        std::numeric_limits<double>::lowest()};
    std::size_t triangleCount = 0;
};

vsg::vec3 toVec3(const gp_Pnt& point)
{
    return vsg::vec3(static_cast<float>(point.X()), static_cast<float>(point.Y()), static_cast<float>(point.Z()));
}

vsg::vec3 toVec3(const gp_Vec& vector)
{
    return vsg::vec3(static_cast<float>(vector.X()), static_cast<float>(vector.Y()), static_cast<float>(vector.Z()));
}

void updateBounds(MeshBuffers& buffers, const gp_Pnt& point)
{
    buffers.min.x = std::min(buffers.min.x, point.X());
    buffers.min.y = std::min(buffers.min.y, point.Y());
    buffers.min.z = std::min(buffers.min.z, point.Z());
    buffers.max.x = std::max(buffers.max.x, point.X());
    buffers.max.y = std::max(buffers.max.y, point.Y());
    buffers.max.z = std::max(buffers.max.z, point.Z());
}

vsg::ref_ptr<vsg::BindGraphicsPipeline> createStepPipeline()
{
    vsg::DescriptorSetLayouts descriptorSetLayouts;
    vsg::PushConstantRanges pushConstantRanges{{VK_SHADER_STAGE_VERTEX_BIT, 0, 128}};
    auto pipelineLayout = vsg::PipelineLayout::create(descriptorSetLayouts, pushConstantRanges);

    auto vertexShaderHints = vsg::ShaderCompileSettings::create();
    auto vertexShader = vsg::ShaderStage::create(VK_SHADER_STAGE_VERTEX_BIT, "main", VERT_SHADER, vertexShaderHints);
    auto fragmentShaderHints = vsg::ShaderCompileSettings::create();
    auto fragmentShader = vsg::ShaderStage::create(VK_SHADER_STAGE_FRAGMENT_BIT, "main", FRAG_SHADER, fragmentShaderHints);
    auto shaderStages = vsg::ShaderStages{vertexShader, fragmentShader};

    auto vertexInputState = vsg::VertexInputState::create();
    auto& bindings = vertexInputState->vertexBindingDescriptions;
    auto& attributes = vertexInputState->vertexAttributeDescriptions;

    constexpr uint32_t offset = 0;
    bindings.emplace_back(VkVertexInputBindingDescription{0, sizeof(vsg::vec3), VK_VERTEX_INPUT_RATE_VERTEX});
    attributes.emplace_back(VkVertexInputAttributeDescription{0, 0, VK_FORMAT_R32G32B32_SFLOAT, offset});

    bindings.emplace_back(VkVertexInputBindingDescription{1, sizeof(vsg::vec3), VK_VERTEX_INPUT_RATE_VERTEX});
    attributes.emplace_back(VkVertexInputAttributeDescription{1, 1, VK_FORMAT_R32G32B32_SFLOAT, offset});

    auto inputAssemblyState = vsg::InputAssemblyState::create();
    inputAssemblyState->topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    auto rasterizationState = vsg::RasterizationState::create();
    rasterizationState->cullMode = VK_CULL_MODE_NONE;

    auto depthStencilState = vsg::DepthStencilState::create();
    depthStencilState->depthTestEnable = VK_TRUE;
    depthStencilState->depthWriteEnable = VK_TRUE;

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

TopoDS_Shape readStepShape(const std::filesystem::path& stepFile)
{
    std::ifstream input(stepFile, std::ios::binary);
    if (!input)
    {
        throw std::runtime_error("Failed to open STEP file: " + stepFile.u8string());
    }

    STEPControl_Reader reader;
    const std::string displayName = stepFile.filename().u8string();
    const auto status = reader.ReadStream(displayName.c_str(), input);
    if (status != IFSelect_RetDone)
    {
        throw std::runtime_error("OCCT failed to read STEP data from: " + stepFile.u8string());
    }

    if (reader.TransferRoots() <= 0)
    {
        throw std::runtime_error("OCCT did not transfer any root shape from: " + stepFile.u8string());
    }

    TopoDS_Shape shape = reader.OneShape();
    if (shape.IsNull())
    {
        throw std::runtime_error("Transferred STEP shape is empty: " + stepFile.u8string());
    }

    return shape;
}

double computeLinearDeflection(const TopoDS_Shape& shape)
{
    Bnd_Box bounds;
    BRepBndLib::Add(shape, bounds, false);
    if (bounds.IsVoid())
    {
        return 0.5;
    }

    double xmin = 0.0;
    double ymin = 0.0;
    double zmin = 0.0;
    double xmax = 0.0;
    double ymax = 0.0;
    double zmax = 0.0;
    bounds.Get(xmin, ymin, zmin, xmax, ymax, zmax);

    const double dx = xmax - xmin;
    const double dy = ymax - ymin;
    const double dz = zmax - zmin;
    const double diagonal = std::sqrt(dx * dx + dy * dy + dz * dz);
    if (diagonal <= 0.0)
    {
        return 0.1;
    }

    return std::clamp(diagonal * 0.002, 0.01, 10.0);
}

MeshBuffers extractMesh(const TopoDS_Shape& shape)
{
    const double linearDeflection = computeLinearDeflection(shape);
    BRepMesh_IncrementalMesh meshGenerator(shape, linearDeflection, false, 0.35, true);
    (void)meshGenerator;

    MeshBuffers buffers;

    for (TopExp_Explorer explorer(shape, TopAbs_FACE); explorer.More(); explorer.Next())
    {
        const TopoDS_Face face = TopoDS::Face(explorer.Current());

        TopLoc_Location location;
        const auto triangulation = BRep_Tool::Triangulation(face, location);
        if (triangulation.IsNull() || triangulation->NbTriangles() == 0)
        {
            continue;
        }

        const gp_Trsf transform = location.Transformation();
        const bool isReversed = face.Orientation() == TopAbs_REVERSED;

        for (int triangleIndex = 1; triangleIndex <= triangulation->NbTriangles(); ++triangleIndex)
        {
            int index1 = 0;
            int index2 = 0;
            int index3 = 0;
            triangulation->Triangle(triangleIndex).Get(index1, index2, index3);

            const std::array<int, 3> nodeIndices = isReversed
                                                       ? std::array<int, 3>{index1, index3, index2}
                                                       : std::array<int, 3>{index1, index2, index3};

            std::array<gp_Pnt, 3> points = {
                triangulation->Node(nodeIndices[0]).Transformed(transform),
                triangulation->Node(nodeIndices[1]).Transformed(transform),
                triangulation->Node(nodeIndices[2]).Transformed(transform)};

            gp_Vec faceNormal = gp_Vec(points[0], points[1]).Crossed(gp_Vec(points[0], points[2]));
            if (faceNormal.SquareMagnitude() <= 1.0e-24)
            {
                continue;
            }
            faceNormal.Normalize();

            ++buffers.triangleCount;

            for (std::size_t vertexIndex = 0; vertexIndex < points.size(); ++vertexIndex)
            {
                buffers.positions.push_back(toVec3(points[vertexIndex]));
                updateBounds(buffers, points[vertexIndex]);

                gp_Vec normal = faceNormal;
                if (triangulation->HasNormals())
                {
                    normal = gp_Vec(triangulation->Normal(nodeIndices[vertexIndex]));
                    normal.Transform(transform);
                    if (isReversed)
                    {
                        normal.Reverse();
                    }

                    if (normal.SquareMagnitude() <= 1.0e-24)
                    {
                        normal = faceNormal;
                    }
                    else
                    {
                        normal.Normalize();
                    }
                }

                buffers.normals.push_back(toVec3(normal));
            }
        }
    }

    if (buffers.triangleCount == 0)
    {
        throw std::runtime_error("No triangulated faces were produced from the STEP model.");
    }

    return buffers;
}

vsg::ref_ptr<vsg::Node> createSceneNode(const MeshBuffers& buffers)
{
    auto positions = vsg::vec3Array::create(static_cast<uint32_t>(buffers.positions.size()));
    auto normals = vsg::vec3Array::create(static_cast<uint32_t>(buffers.normals.size()));
    auto indices = vsg::uintArray::create(static_cast<uint32_t>(buffers.positions.size()));

    for (std::size_t i = 0; i < buffers.positions.size(); ++i)
    {
        (*positions)[static_cast<uint32_t>(i)] = buffers.positions[i];
        (*normals)[static_cast<uint32_t>(i)] = buffers.normals[i];
        (*indices)[static_cast<uint32_t>(i)] = static_cast<uint32_t>(i);
    }

    auto drawCommands = vsg::VertexIndexDraw::create();
    drawCommands->assignArrays(vsg::DataList{positions, normals});
    drawCommands->assignIndices(indices);
    drawCommands->indexCount = indices->width();
    drawCommands->instanceCount = 1;

    auto stateGroup = vsg::StateGroup::create();
    stateGroup->add(createStepPipeline());
    stateGroup->addChild(drawCommands);

    return stateGroup;
}
} // namespace

StepSceneData loadStepScene(const std::filesystem::path& stepFile)
{
    const TopoDS_Shape shape = readStepShape(stepFile);
    const MeshBuffers buffers = extractMesh(shape);

    StepSceneData sceneData;
    sceneData.scene = createSceneNode(buffers);
    sceneData.center = (buffers.min + buffers.max) * 0.5;
    sceneData.radius = vsg::length(buffers.max - buffers.min) * 0.5;
    sceneData.radius = std::max(sceneData.radius, 1.0);
    sceneData.triangleCount = buffers.triangleCount;

    return sceneData;
}
} // namespace vsgocct
