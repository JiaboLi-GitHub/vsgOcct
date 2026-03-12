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
// 这里直接以内嵌 GLSL 字符串创建最小可用的着色器，
// 避免示例项目额外依赖外部 shader 文件，便于快速加载和分发。
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

// 从 OCCT 三角化结果提取出来的中间缓冲。
// 之所以先落在 CPU 侧结构里，而不是边遍历边创建 VSG 对象，
// 是为了同时完成包围盒统计、法线补全和三角形计数，最后再一次性上传。
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

// OCCT 使用双精度几何类型，VSG 顶点缓冲这里采用 float 即可满足渲染需要，
// 因此在进入渲染数据结构前做一次显式转换。
vsg::vec3 toVec3(const gp_Pnt& point)
{
    return vsg::vec3(static_cast<float>(point.X()), static_cast<float>(point.Y()), static_cast<float>(point.Z()));
}

vsg::vec3 toVec3(const gp_Vec& vector)
{
    return vsg::vec3(static_cast<float>(vector.X()), static_cast<float>(vector.Y()), static_cast<float>(vector.Z()));
}

// 持续更新模型的轴对齐包围盒，后续可用于计算场景中心和初始观察半径。
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
    // 示例使用 push constant 直接传投影矩阵和观察矩阵，
    // 管线足够轻量，也省去了 descriptor set 的额外配置。
    vsg::DescriptorSetLayouts descriptorSetLayouts;
    vsg::PushConstantRanges pushConstantRanges{{VK_SHADER_STAGE_VERTEX_BIT, 0, 128}};
    auto pipelineLayout = vsg::PipelineLayout::create(descriptorSetLayouts, pushConstantRanges);

    // 运行时编译内嵌 shader，方便示例工程独立运行。
    auto vertexShaderHints = vsg::ShaderCompileSettings::create();
    auto vertexShader = vsg::ShaderStage::create(VK_SHADER_STAGE_VERTEX_BIT, "main", VERT_SHADER, vertexShaderHints);
    auto fragmentShaderHints = vsg::ShaderCompileSettings::create();
    auto fragmentShader = vsg::ShaderStage::create(VK_SHADER_STAGE_FRAGMENT_BIT, "main", FRAG_SHADER, fragmentShaderHints);
    auto shaderStages = vsg::ShaderStages{vertexShader, fragmentShader};

    auto vertexInputState = vsg::VertexInputState::create();
    auto& bindings = vertexInputState->vertexBindingDescriptions;
    auto& attributes = vertexInputState->vertexAttributeDescriptions;

    // 绑定 0 放顶点坐标，绑定 1 放法线；
    // 两者都按“每个顶点一条记录”的方式输入到管线。
    constexpr uint32_t offset = 0;
    bindings.emplace_back(VkVertexInputBindingDescription{0, sizeof(vsg::vec3), VK_VERTEX_INPUT_RATE_VERTEX});
    attributes.emplace_back(VkVertexInputAttributeDescription{0, 0, VK_FORMAT_R32G32B32_SFLOAT, offset});

    bindings.emplace_back(VkVertexInputBindingDescription{1, sizeof(vsg::vec3), VK_VERTEX_INPUT_RATE_VERTEX});
    attributes.emplace_back(VkVertexInputAttributeDescription{1, 1, VK_FORMAT_R32G32B32_SFLOAT, offset});

    auto inputAssemblyState = vsg::InputAssemblyState::create();
    inputAssemblyState->topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    auto rasterizationState = vsg::RasterizationState::create();
    // CAD 模型经常会出现面朝向不完全一致的情况，关闭剔除可以减少“缺面”现象，
    // 配合法线中的 gl_FrontFacing 修正，让双面都能得到稳定光照。
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
    // ReadStream 需要一个输入流，这里先在 C++ 标准库层面确认文件可读，
    // 这样错误信息也能更明确地指出是“文件打不开”还是“格式解析失败”。
    std::ifstream input(stepFile, std::ios::binary);
    if (!input)
    {
        throw std::runtime_error("Failed to open STEP file: " + stepFile.u8string());
    }

    STEPControl_Reader reader;
    // 给流指定一个显示名，便于 OCCT 在内部诊断信息中标识当前模型来源。
    const std::string displayName = stepFile.filename().u8string();
    const auto status = reader.ReadStream(displayName.c_str(), input);
    if (status != IFSelect_RetDone)
    {
        throw std::runtime_error("OCCT failed to read STEP data from: " + stepFile.u8string());
    }

    if (reader.TransferRoots() <= 0)
    {
        // STEP 文件可能语法合法，但没有成功转换出可用的拓扑根节点；
        // 这里单独报错，方便区分“读文件成功但转形体失败”的场景。
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
    // 线性偏差控制三角化精度：值越小，网格越密、细节保留越多，但生成成本更高。
    // 这里根据整体包围盒尺寸自适应估算，让大模型和小模型都得到相对合理的采样密度。
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

    // 给出上下限，避免极小模型生成过密网格，也避免超大模型精度过粗。
    return std::clamp(diagonal * 0.002, 0.01, 10.0);
}

MeshBuffers extractMesh(const TopoDS_Shape& shape)
{
    const double linearDeflection = computeLinearDeflection(shape);
    // 触发 OCCT 的面片离散化。
    // false: 不并行法线角度控制；0.35: 角度偏差；true: 允许在已有网格基础上优化。
    BRepMesh_IncrementalMesh meshGenerator(shape, linearDeflection, false, 0.35, true);
    (void)meshGenerator;

    MeshBuffers buffers;

    // STEP 模型的可渲染几何主要来自 Face，因此按面遍历并抽取每个面的三角片。
    for (TopExp_Explorer explorer(shape, TopAbs_FACE); explorer.More(); explorer.Next())
    {
        const TopoDS_Face face = TopoDS::Face(explorer.Current());

        TopLoc_Location location;
        const auto triangulation = BRep_Tool::Triangulation(face, location);
        if (triangulation.IsNull() || triangulation->NbTriangles() == 0)
        {
            // 某些面可能没有成功离散化，或本身不携带三角数据，直接跳过即可。
            continue;
        }

        const gp_Trsf transform = location.Transformation();
        // OCCT 的面可能带有“反向”拓扑语义。这里记录下来，后面需要同时修正顶点顺序和法线方向。
        const bool isReversed = face.Orientation() == TopAbs_REVERSED;

        for (int triangleIndex = 1; triangleIndex <= triangulation->NbTriangles(); ++triangleIndex)
        {
            int index1 = 0;
            int index2 = 0;
            int index3 = 0;
            triangulation->Triangle(triangleIndex).Get(index1, index2, index3);

            // 如果面是反向的，需要交换三角形的绕序；
            // 否则法线方向会与几何外法线不一致，影响光照和正反面判断。
            const std::array<int, 3> nodeIndices = isReversed
                                                       ? std::array<int, 3>{index1, index3, index2}
                                                       : std::array<int, 3>{index1, index2, index3};

            // Triangulation 中的节点坐标是局部坐标，需要乘上 location 变换后才是世界坐标。
            std::array<gp_Pnt, 3> points = {
                triangulation->Node(nodeIndices[0]).Transformed(transform),
                triangulation->Node(nodeIndices[1]).Transformed(transform),
                triangulation->Node(nodeIndices[2]).Transformed(transform)};

            // 无论模型是否自带顶点法线，都先由三角形几何关系算一个面法线，
            // 作为默认值和异常情况下的兜底法线。
            gp_Vec faceNormal = gp_Vec(points[0], points[1]).Crossed(gp_Vec(points[0], points[2]));
            if (faceNormal.SquareMagnitude() <= 1.0e-24)
            {
                // 面积几乎为 0 的退化三角形没有稳定法线，也没有实际渲染价值。
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
                    // 如果 OCCT 已经给出更平滑的顶点法线，则优先使用它，
                    // 这样曲面看起来不会像纯平面着色那样生硬。
                    normal = gp_Vec(triangulation->Normal(nodeIndices[vertexIndex]));
                    normal.Transform(transform);
                    if (isReversed)
                    {
                        normal.Reverse();
                    }

                    if (normal.SquareMagnitude() <= 1.0e-24)
                    {
                        // 顶点法线异常时退回到面法线，保证渲染阶段不会出现零向量归一化。
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
        // 能读到 STEP 拓扑但没有成功生成任何可渲染三角形时，直接抛错给上层处理。
        throw std::runtime_error("No triangulated faces were produced from the STEP model.");
    }

    return buffers;
}

vsg::ref_ptr<vsg::Node> createSceneNode(const MeshBuffers& buffers)
{
    // 这里采用“非索引复用”的简单上传方式：每个三角形顶点都展开存储。
    // 对示例项目来说逻辑最直接，也更方便与 OCCT 面级遍历结果一一对应。
    auto positions = vsg::vec3Array::create(static_cast<uint32_t>(buffers.positions.size()));
    auto normals = vsg::vec3Array::create(static_cast<uint32_t>(buffers.normals.size()));
    auto indices = vsg::uintArray::create(static_cast<uint32_t>(buffers.positions.size()));

    for (std::size_t i = 0; i < buffers.positions.size(); ++i)
    {
        (*positions)[static_cast<uint32_t>(i)] = buffers.positions[i];
        (*normals)[static_cast<uint32_t>(i)] = buffers.normals[i];
        (*indices)[static_cast<uint32_t>(i)] = static_cast<uint32_t>(i);
    }

    // VertexIndexDraw 封装了顶点数组和索引绘制命令，
    // 这里把所有三角形作为一个批次提交给 GPU。
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
    // 整个加载流程刻意拆成“读 STEP -> 提取网格 -> 建场景”三步，
    // 这样后续如果要缓存网格、替换渲染后端或插入额外处理，会更容易扩展。
    const TopoDS_Shape shape = readStepShape(stepFile);
    const MeshBuffers buffers = extractMesh(shape);

    StepSceneData sceneData;
    sceneData.scene = createSceneNode(buffers);
    // 包围盒中心用于初始相机对焦；半径用于设置观察距离和裁剪面。
    sceneData.center = (buffers.min + buffers.max) * 0.5;
    sceneData.radius = vsg::length(buffers.max - buffers.min) * 0.5;
    sceneData.radius = std::max(sceneData.radius, 1.0);
    sceneData.triangleCount = buffers.triangleCount;
    
    return sceneData;
}
} // namespace vsgocct
