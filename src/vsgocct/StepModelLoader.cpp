#include <vsgocct/StepModelLoader.h>

#include <vsgocct/cad/StepReader.h>

#include <BRepAdaptor_Curve.hxx>
#include <BRepBndLib.hxx>
#include <BRepMesh_IncrementalMesh.hxx>
#include <BRep_Tool.hxx>
#include <Bnd_Box.hxx>
#include <GCPnts_QuasiUniformDeflection.hxx>
#include <NCollection_IndexedMap.hxx>
#include <Poly_Polygon3D.hxx>
#include <Poly_PolygonOnTriangulation.hxx>
#include <Poly_Triangulation.hxx>
#include <Standard_Failure.hxx>
#include <TopAbs_Orientation.hxx>
#include <TopAbs_ShapeEnum.hxx>
#include <TopExp.hxx>
#include <TopLoc_Location.hxx>
#include <TopTools_ShapeMapHasher.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Vertex.hxx>
#include <gp_Pnt.hxx>
#include <gp_Trsf.hxx>
#include <gp_Vec.hxx>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace vsgocct
{
namespace
{
// 面渲染使用基础的法线光照。
// 这里故意把 shader 以内嵌字符串的形式放在源码里，
// 这样示例工程不依赖额外的 shader 文件，便于直接编译和分发。
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

// 线渲染不需要法线，只需把顶点变换到裁剪空间并输出固定颜色。
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

// 点渲染通过 gl_PointSize 生成屏幕空间圆点，便于直接观察 STEP 顶点拓扑。
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

// 用带索引的拓扑 map 收集唯一的子形体，避免同一个 Vertex/Edge/Face 被重复提取。
using IndexedShapeMap = NCollection_IndexedMap<TopoDS_Shape, TopTools_ShapeMapHasher>;

// 在点、线、面三类几何都可能独立存在的前提下，
// 包围盒统计不能只依赖面，因此单独做一个公共累加器。
struct BoundsAccumulator
{
    vsg::dvec3 min = {
        std::numeric_limits<double>::max(),
        std::numeric_limits<double>::max(),
        std::numeric_limits<double>::max()};
    vsg::dvec3 max = {
        std::numeric_limits<double>::lowest(),
        std::numeric_limits<double>::lowest(),
        std::numeric_limits<double>::lowest()};
};

// 点渲染只需要位置数组，pointCount 单独记录“逻辑点数量”，
// 这样即使后续改为实例化渲染，统计口径也不会被 GPU 缓冲布局影响。
struct PointBuffers
{
    std::vector<vsg::vec3> positions;
    std::size_t pointCount = 0;
};

// 线渲染当前采用 line list，因此 positions 中每两项构成一条线段。
struct LineBuffers
{
    std::vector<vsg::vec3> positions;
    std::size_t segmentCount = 0;
};

// 面渲染需要位置和法线，triangleCount 用于界面显示和复杂度统计。
struct FaceBuffers
{
    std::vector<vsg::vec3> positions;
    std::vector<vsg::vec3> normals;
    std::size_t triangleCount = 0;
};

// SceneBuffers 是 CPU 侧的中间结果。
// 先把点、线、面都提取到这里，再统一创建 VSG 节点，
// 可以把“几何提取”和“渲染对象构建”两个阶段清晰地分开。
struct SceneBuffers
{
    PointBuffers points;
    LineBuffers lines;
    FaceBuffers faces;
    BoundsAccumulator bounds;

    bool hasGeometry() const
    {
        return pointCount() > 0 || lineSegmentCount() > 0 || triangleCount() > 0;
    }

    std::size_t pointCount() const { return points.pointCount; }
    std::size_t lineSegmentCount() const { return lines.segmentCount; }
    std::size_t triangleCount() const { return faces.triangleCount; }
};

// SceneNodes 是 GPU / 场景图层面的结果。
// 与 SceneBuffers 对应，但更偏向渲染组织和上层控制。
struct SceneNodes
{
    vsg::ref_ptr<vsg::Node> scene;
    vsg::ref_ptr<vsg::Switch> pointSwitch;
    vsg::ref_ptr<vsg::Switch> lineSwitch;
    vsg::ref_ptr<vsg::Switch> faceSwitch;
};

// OCCT 以双精度存几何，VSG 示例渲染这里用 float 就足够，
// 因此统一通过这两个小工具做显式转换，避免散落的 static_cast。
vsg::vec3 toVec3(const gp_Pnt& point)
{
    return vsg::vec3(static_cast<float>(point.X()), static_cast<float>(point.Y()), static_cast<float>(point.Z()));
}

vsg::vec3 toVec3(const gp_Vec& vector)
{
    return vsg::vec3(static_cast<float>(vector.X()), static_cast<float>(vector.Y()), static_cast<float>(vector.Z()));
}

// 所有 primitive 共用一个包围盒，这样模型即使只有点或只有线，也能得到正确的相机初始视角。
void updateBounds(BoundsAccumulator& bounds, const gp_Pnt& point)
{
    bounds.min.x = std::min(bounds.min.x, point.X());
    bounds.min.y = std::min(bounds.min.y, point.Y());
    bounds.min.z = std::min(bounds.min.z, point.Z());
    bounds.max.x = std::max(bounds.max.x, point.X());
    bounds.max.y = std::max(bounds.max.y, point.Y());
    bounds.max.z = std::max(bounds.max.z, point.Z());
}

// 点提取的入口很简单，但仍然统一走这个函数，
// 这样统计和包围盒更新逻辑不会在调用处重复。
void appendPoint(PointBuffers& buffers, BoundsAccumulator& bounds, const gp_Pnt& point)
{
    buffers.positions.push_back(toVec3(point));
    updateBounds(bounds, point);
    ++buffers.pointCount;
}

// 线段是线渲染的最小单位。
// 这里先过滤掉长度几乎为 0 的退化段，避免无意义数据进入 GPU。
void appendLineSegment(LineBuffers& buffers, BoundsAccumulator& bounds, const gp_Pnt& start, const gp_Pnt& end)
{
    if (gp_Vec(start, end).SquareMagnitude() <= 1.0e-24)
    {
        return;
    }

    buffers.positions.push_back(toVec3(start));
    buffers.positions.push_back(toVec3(end));
    updateBounds(bounds, start);
    updateBounds(bounds, end);
    ++buffers.segmentCount;
}

template<class PointAccessor>
bool appendPolyline(LineBuffers& buffers, BoundsAccumulator& bounds, int pointCount, PointAccessor&& pointAccessor)
{
    // 多段线最终都会被展开成 line list。
    // 这里通过传入 pointAccessor，把“点从哪里来”的差异抽象掉：
    // 既可以来自 Poly_Polygon3D，也可以来自 Triangulation，或直接来自曲线采样。
    if (pointCount < 2)
    {
        return false;
    }

    gp_Pnt previous = pointAccessor(1);
    bool appended = false;
    for (int pointIndex = 2; pointIndex <= pointCount; ++pointIndex)
    {
        const gp_Pnt current = pointAccessor(pointIndex);
        const std::size_t segmentCountBefore = buffers.segmentCount;
        appendLineSegment(buffers, bounds, previous, current);
        appended = appended || buffers.segmentCount != segmentCountBefore;
        previous = current;
    }

    return appended;
}

vsg::ref_ptr<vsg::BindGraphicsPipeline> createPrimitivePipeline(
    const char* vertexShaderSource,
    const char* fragmentShaderSource,
    VkPrimitiveTopology topology,
    bool includeNormals,
    bool depthWrite)
{
    // 示例里统一使用 push constant 传 projection/modelView，
    // 这样对点、线、面三种简单 primitive 都够用，也能避免 descriptor set 配置噪音。
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

    // binding 0 恒定用于位置；
    // 只有面渲染才会额外启用 binding 1 作为法线输入。
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

    // CAD 数据里正反面朝向未必总是一致，统一关闭剔除可以减少“缺面/缺边”的视觉异常。
    auto rasterizationState = vsg::RasterizationState::create();
    rasterizationState->cullMode = VK_CULL_MODE_NONE;
    rasterizationState->lineWidth = 1.0f;

    // 点和线作为覆盖层显示时通常不应覆写深度，否则容易把后面的面“戳穿”。
    // 因此这里把是否写深度暴露成参数。
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
    // 点和线都只依赖位置数组，因此共用这套上传逻辑。
    if (positions.empty())
    {
        return vsg::Group::create();
    }

    auto positionArray = vsg::vec3Array::create(static_cast<uint32_t>(positions.size()));
    auto indices = vsg::uintArray::create(static_cast<uint32_t>(positions.size()));

    // 这里采用最直观的“展开后按顺序绘制”方式，
    // 便于保持和提取阶段一一对应，代价是放弃了一部分索引复用。
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

vsg::ref_ptr<vsg::Node> createFaceNode(const FaceBuffers& buffers)
{
    // 面渲染单独保留一份构建逻辑，因为它需要同时上传位置和法线。
    if (buffers.positions.empty())
    {
        return vsg::Group::create();
    }

    auto positions = vsg::vec3Array::create(static_cast<uint32_t>(buffers.positions.size()));
    auto normals = vsg::vec3Array::create(static_cast<uint32_t>(buffers.normals.size()));
    auto indices = vsg::uintArray::create(static_cast<uint32_t>(buffers.positions.size()));

    for (std::size_t index = 0; index < buffers.positions.size(); ++index)
    {
        (*positions)[static_cast<uint32_t>(index)] = buffers.positions[index];
        (*normals)[static_cast<uint32_t>(index)] = buffers.normals[index];
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

vsg::ref_ptr<vsg::Switch> createPrimitiveSwitch(const vsg::ref_ptr<vsg::Node>& node)
{
    // 每类 primitive 都包进一个独立 Switch，
    // 这样上层显隐控制只需切 mask，不需要重新编译或重建场景。
    auto primitiveSwitch = vsg::Switch::create();
    primitiveSwitch->addChild(true, node ? node : vsg::Group::create());
    return primitiveSwitch;
}

double computeLinearDeflection(const TopoDS_Shape& shape)
{
    // STEP 三角化精度采用包围盒尺度自适应：
    // 模型越大，允许的离散偏差越大；模型越小，则自动收紧偏差。
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

void extractPoints(const TopoDS_Shape& shape, SceneBuffers& buffers)
{
    // 这里按唯一 Vertex 收集点拓扑，适合用于调试、标注或后续拾取辅助显示。
    IndexedShapeMap vertexMap;
    TopExp::MapShapes(shape, TopAbs_VERTEX, vertexMap);

    for (int vertexIndex = 1; vertexIndex <= vertexMap.Extent(); ++vertexIndex)
    {
        const TopoDS_Vertex vertex = TopoDS::Vertex(vertexMap.FindKey(vertexIndex));
        appendPoint(buffers.points, buffers.bounds, BRep_Tool::Pnt(vertex));
    }
}

bool appendEdgeFromPolygon3D(const TopoDS_Edge& edge, SceneBuffers& buffers)
{
    // 优先尝试直接读取边自带的 3D polyline。
    // 如果 STEP/OCCT 已经给出这份离散结果，这是成本最低也最贴近原始拓扑表达的路径。
    TopLoc_Location location;
    const auto polygon = BRep_Tool::Polygon3D(edge, location);
    if (polygon.IsNull() || polygon->NbNodes() < 2)
    {
        return false;
    }

    const gp_Trsf transform = location.Transformation();
    return appendPolyline(
        buffers.lines,
        buffers.bounds,
        polygon->NbNodes(),
        [&](int pointIndex)
        {
            return polygon->Nodes().Value(pointIndex).Transformed(transform);
        });
}

bool appendEdgeFromPolygonOnTriangulation(const TopoDS_Edge& edge, SceneBuffers& buffers)
{
    // 某些边虽然没有独立的 3D polygon，但会挂在某个面三角化结果上。
    // 这时可以借 triangulation 上的节点索引还原出边的折线。
    occ::handle<Poly_PolygonOnTriangulation> polygon;
    occ::handle<Poly_Triangulation> triangulation;
    TopLoc_Location location;
    BRep_Tool::PolygonOnTriangulation(edge, polygon, triangulation, location);
    if (polygon.IsNull() || triangulation.IsNull() || polygon->NbNodes() < 2)
    {
        return false;
    }

    const gp_Trsf transform = location.Transformation();
    return appendPolyline(
        buffers.lines,
        buffers.bounds,
        polygon->NbNodes(),
        [&](int pointIndex)
        {
            return triangulation->Node(polygon->Node(pointIndex)).Transformed(transform);
        });
}

bool appendEdgeFromCurve(const TopoDS_Edge& edge, double linearDeflection, SceneBuffers& buffers)
{
    // 当前两种缓存形式都拿不到时，最后回退到“基于解析曲线实时采样”。
    // 这样即使边没有附带离散数据，也尽量保证线框能显示出来。
    try
    {
        BRepAdaptor_Curve curve(edge);
        const double first = curve.FirstParameter();
        const double last = curve.LastParameter();
        if (!std::isfinite(first) || !std::isfinite(last) || std::abs(last - first) <= 1.0e-12)
        {
            return false;
        }

        GCPnts_QuasiUniformDeflection sampler(curve, linearDeflection, first, last);
        if (sampler.IsDone() && sampler.NbPoints() >= 2)
        {
            return appendPolyline(
                buffers.lines,
                buffers.bounds,
                sampler.NbPoints(),
                [&](int pointIndex)
                {
                    return sampler.Value(pointIndex);
                });
        }

        // 采样失败时再退一步，至少保留首末点连线，避免整条边完全消失。
        appendLineSegment(buffers.lines, buffers.bounds, curve.Value(first), curve.Value(last));
        return true;
    }
    catch (const Standard_Failure&)
    {
        // OCCT 在个别异常边上可能抛 Standard_Failure，
        // 这里选择吞掉并返回 false，让调用方继续尝试其它边，而不是中断整个模型加载。
        return false;
    }
}

void extractLines(const TopoDS_Shape& shape, double linearDeflection, SceneBuffers& buffers)
{
    // 边提取按“已有 3D polygon -> triangulation polygon -> 曲线采样”的顺序回退，
    // 目标是在稳妥和完整性之间取得平衡。
    IndexedShapeMap edgeMap;
    TopExp::MapShapes(shape, TopAbs_EDGE, edgeMap);

    for (int edgeIndex = 1; edgeIndex <= edgeMap.Extent(); ++edgeIndex)
    {
        const TopoDS_Edge edge = TopoDS::Edge(edgeMap.FindKey(edgeIndex));
        if (BRep_Tool::Degenerated(edge))
        {
            // 退化边通常没有稳定几何长度，跳过更安全。
            continue;
        }

        if (appendEdgeFromPolygon3D(edge, buffers))
        {
            continue;
        }

        if (appendEdgeFromPolygonOnTriangulation(edge, buffers))
        {
            continue;
        }

        (void)appendEdgeFromCurve(edge, linearDeflection, buffers);
    }
}

void extractFaces(const TopoDS_Shape& shape, SceneBuffers& buffers)
{
    // 面仍然通过 OCCT triangulation 提取。
    // 这是当前渲染器里最成熟的一条路径，也负责提供稳定的包围盒和法线数据。
    IndexedShapeMap faceMap;
    TopExp::MapShapes(shape, TopAbs_FACE, faceMap);

    for (int faceIndex = 1; faceIndex <= faceMap.Extent(); ++faceIndex)
    {
        const TopoDS_Face face = TopoDS::Face(faceMap.FindKey(faceIndex));

        TopLoc_Location location;
        const auto triangulation = BRep_Tool::Triangulation(face, location);
        if (triangulation.IsNull() || triangulation->NbTriangles() == 0)
        {
            continue;
        }

        const gp_Trsf transform = location.Transformation();
        // 面在拓扑上可能被标记为 REVERSED。
        // 这里后续会同时修正三角形绕序和法线方向，避免光照与正反面判断错乱。
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

            // Triangulation 节点是局部坐标，必须乘上 location 变换后才能用于统一场景。
            std::array<gp_Pnt, 3> points = {
                triangulation->Node(nodeIndices[0]).Transformed(transform),
                triangulation->Node(nodeIndices[1]).Transformed(transform),
                triangulation->Node(nodeIndices[2]).Transformed(transform)};

            // 先用几何关系算一个面法线，既能作为默认法线，也能在顶点法线异常时兜底。
            gp_Vec faceNormal = gp_Vec(points[0], points[1]).Crossed(gp_Vec(points[0], points[2]));
            if (faceNormal.SquareMagnitude() <= 1.0e-24)
            {
                // 退化三角形面积几乎为 0，没有实际渲染意义。
                continue;
            }
            faceNormal.Normalize();

            ++buffers.faces.triangleCount;

            for (std::size_t vertexIndex = 0; vertexIndex < points.size(); ++vertexIndex)
            {
                buffers.faces.positions.push_back(toVec3(points[vertexIndex]));
                updateBounds(buffers.bounds, points[vertexIndex]);

                gp_Vec normal = faceNormal;
                if (triangulation->HasNormals())
                {
                    // 如果 OCCT 提供了更平滑的顶点法线，则优先使用，
                    // 曲面观感会明显好于纯面法线着色。
                    normal = gp_Vec(triangulation->Normal(nodeIndices[vertexIndex]));
                    normal.Transform(transform);
                    if (isReversed)
                    {
                        normal.Reverse();
                    }

                    if (normal.SquareMagnitude() <= 1.0e-24)
                    {
                        // 顶点法线异常时回退到面法线，保证着色阶段不会遇到零向量。
                        normal = faceNormal;
                    }
                    else
                    {
                        normal.Normalize();
                    }
                }

                buffers.faces.normals.push_back(toVec3(normal));
            }
        }
    }
}

SceneBuffers extractSceneBuffers(const TopoDS_Shape& shape)
{
    // 先触发一次全形体三角化。
    // 面渲染直接使用结果，线提取里也可能复用 PolygonOnTriangulation。
    const double linearDeflection = computeLinearDeflection(shape);
    BRepMesh_IncrementalMesh meshGenerator(shape, linearDeflection, false, 0.35, true);
    (void)meshGenerator;

    // 三类 primitive 独立提取，但共享统计和包围盒。
    SceneBuffers buffers;
    extractPoints(shape, buffers);
    extractLines(shape, linearDeflection, buffers);
    extractFaces(shape, buffers);

    if (!buffers.hasGeometry())
    {
        // 只要点、线、面三者都没有任何可渲染结果，就认为整个模型不可显示。
        throw std::runtime_error("No renderable points, lines, or faces were produced from the STEP model.");
    }

    return buffers;
}

SceneNodes createSceneNodes(const SceneBuffers& buffers)
{
    // 三类节点分别建管线、分别包 Switch，保证后续控制粒度足够细。
    SceneNodes sceneNodes;
    sceneNodes.pointSwitch = createPrimitiveSwitch(createPositionOnlyNode(
        buffers.points.positions,
        createPrimitivePipeline(POINT_VERT_SHADER, POINT_FRAG_SHADER, VK_PRIMITIVE_TOPOLOGY_POINT_LIST, false, false)));
    sceneNodes.lineSwitch = createPrimitiveSwitch(createPositionOnlyNode(
        buffers.lines.positions,
        createPrimitivePipeline(LINE_VERT_SHADER, LINE_FRAG_SHADER, VK_PRIMITIVE_TOPOLOGY_LINE_LIST, false, false)));
    sceneNodes.faceSwitch = createPrimitiveSwitch(createFaceNode(buffers.faces));

    // 绘制顺序上先面、再线、再点。
    // 面负责主体实体感，线和点作为覆盖层更容易被看清。
    auto root = vsg::Group::create();
    root->addChild(sceneNodes.faceSwitch);
    root->addChild(sceneNodes.lineSwitch);
    root->addChild(sceneNodes.pointSwitch);
    sceneNodes.scene = root;
    return sceneNodes;
}
} // namespace

StepSceneData loadStepScene(const std::filesystem::path& stepFile)
{
    // 顶层流程保持成”读 STEP -> 提取三类几何 -> 组装场景 -> 回填统计”的顺序，
    // 后续如果要插入缓存、材质或拾取数据，也更容易扩展。
    const auto shapeData = cad::readStep(stepFile);
    const TopoDS_Shape& shape = shapeData.shape;
    const SceneBuffers buffers = extractSceneBuffers(shape);
    const SceneNodes sceneNodes = createSceneNodes(buffers);

    StepSceneData sceneData;
    sceneData.scene = sceneNodes.scene;
    sceneData.pointSwitch = sceneNodes.pointSwitch;
    sceneData.lineSwitch = sceneNodes.lineSwitch;
    sceneData.faceSwitch = sceneNodes.faceSwitch;
    // 即使模型只有点或只有线，这里的 bounds 也已经在提取阶段被完整维护。
    sceneData.center = (buffers.bounds.min + buffers.bounds.max) * 0.5;
    sceneData.radius = vsg::length(buffers.bounds.max - buffers.bounds.min) * 0.5;
    sceneData.radius = std::max(sceneData.radius, 1.0);
    sceneData.pointCount = buffers.pointCount();
    sceneData.lineSegmentCount = buffers.lineSegmentCount();
    sceneData.triangleCount = buffers.triangleCount();
    return sceneData;
}
} // namespace vsgocct
