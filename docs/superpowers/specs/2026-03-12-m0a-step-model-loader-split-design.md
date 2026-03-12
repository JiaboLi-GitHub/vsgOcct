# M0a: StepModelLoader 职责拆分设计

## 概述

将当前单体 `StepModelLoader`（~800 行）拆分为三个独立模块：

- `vsgocct::cad` — STEP 文件读取
- `vsgocct::mesh` — 几何三角化
- `vsgocct::scene` — VSG 场景构建

保留 `loadStepScene()` 作为 facade，示例程序无需修改。

## 设计决策

| 决策 | 选择 | 理由 |
|------|------|------|
| 文件组织 | 按子命名空间分目录 | 为后续模块扩展做准备 |
| API 风格 | 自由函数 + Options 结构体 | 无状态、线程安全、与 ROADMAP 目标 API 一致 |
| 向后兼容 | 保留 `loadStepScene()` 作为 facade | 示例程序零改动 |
| Options | 同步引入 | 避免二次破坏 API |

## 文件结构

```
include/vsgocct/
├── StepModelLoader.h          # 保留 facade API
├── cad/
│   └── StepReader.h           # readStep() + ReaderOptions + ShapeData
├── mesh/
│   └── ShapeMesher.h          # triangulate() + MeshOptions + MeshResult
└── scene/
    └── SceneBuilder.h         # buildScene() + SceneOptions

src/vsgocct/
├── StepModelLoader.cpp        # 瘦身为 facade 实现
├── cad/
│   └── StepReader.cpp         # STEP 读取实现
├── mesh/
│   └── ShapeMesher.cpp        # 几何提取 + 三角化实现
└── scene/
    └── SceneBuilder.cpp       # VSG 场景构建 + shaders + pipeline
```

## API 设计

### cad 模块

```cpp
// include/vsgocct/cad/StepReader.h
#pragma once

#include <filesystem>
#include <TopoDS_Shape.hxx>

namespace vsgocct::cad
{
struct ReaderOptions
{
    // 预留扩展
};

struct ShapeData
{
    TopoDS_Shape shape;
};

ShapeData readStep(const std::filesystem::path& stepFile,
                   const ReaderOptions& options = {});
} // namespace vsgocct::cad
```

**职责**：
- 打开 STEP 文件并验证可读性
- 通过 STEPControl_Reader 解析 STEP 数据
- 传输根形状，返回 TopoDS_Shape

**错误处理**：文件无法打开、STEP 解析失败、无根形状时抛 `std::runtime_error`。

### mesh 模块

```cpp
// include/vsgocct/mesh/ShapeMesher.h
#pragma once

#include <cstddef>
#include <vector>

#include <TopoDS_Shape.hxx>
#include <vsg/maths/vec3.h>

namespace vsgocct::mesh
{
struct MeshOptions
{
    double linearDeflection = 0.0;   // 0 = 根据模型尺寸自动计算
    double angularDeflection = 0.35; // 弧度
    bool relative = false;
};

struct MeshResult
{
    // 点
    std::vector<vsg::vec3> pointPositions;
    std::size_t pointCount = 0;

    // 线
    std::vector<vsg::vec3> linePositions;
    std::size_t lineSegmentCount = 0;

    // 面
    std::vector<vsg::vec3> facePositions;
    std::vector<vsg::vec3> faceNormals;
    std::size_t triangleCount = 0;

    // 包围信息
    vsg::dvec3 boundsMin;
    vsg::dvec3 boundsMax;

    bool hasGeometry() const;
};

MeshResult triangulate(const TopoDS_Shape& shape,
                       const MeshOptions& options = {});
} // namespace vsgocct::mesh
```

**职责**：
- 调用 BRepMesh_IncrementalMesh 触发全形体三角化
- 按拓扑提取 Vertex（点）、Edge（线）、Face（面）
- 线提取三级回退：Polygon3D -> PolygonOnTriangulation -> 曲线采样
- 计算包围盒
- linearDeflection = 0 时自动根据包围盒对角线计算

**内部实现细节（不暴露）**：
- `BoundsAccumulator`
- `PointBuffers`, `LineBuffers`, `FaceBuffers`, `SceneBuffers`
- `toVec3()`, `updateBounds()`, `appendPoint()`, `appendLineSegment()`, `appendPolyline()`
- `extractPoints()`, `extractLines()`, `extractFaces()`
- `appendEdgeFromPolygon3D()`, `appendEdgeFromPolygonOnTriangulation()`, `appendEdgeFromCurve()`
- `computeLinearDeflection()`

**错误处理**：无可渲染几何时抛 `std::runtime_error`。

### scene 模块

```cpp
// include/vsgocct/scene/SceneBuilder.h
#pragma once

#include <vsgocct/StepModelLoader.h>
#include <vsgocct/mesh/ShapeMesher.h>

namespace vsgocct::scene
{
struct SceneOptions
{
    bool pointsVisible = true;
    bool linesVisible = true;
    bool facesVisible = true;
};

StepSceneData buildScene(const mesh::MeshResult& meshResult,
                         const SceneOptions& options = {});
} // namespace vsgocct::scene
```

**职责**：
- 为点、线、面创建独立的 Vulkan 图形管线
- 编译内嵌 GLSL 450 shader
- 将 CPU 侧几何数据上传为 VSG 节点
- 每类 primitive 包进独立的 vsg::Switch
- 根据 SceneOptions 设置初始显隐状态
- 计算 center/radius 并填充 StepSceneData

**内部实现细节（不暴露）**：
- 6 个 GLSL shader 字符串常量
- `createPrimitivePipeline()`
- `createPositionOnlyNode()`
- `createFaceNode()`
- `createPrimitiveSwitch()`
- `SceneNodes` 内部结构体

### facade

```cpp
// StepModelLoader.cpp
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
```

## 代码搬迁映射

| 原位置（StepModelLoader.cpp） | 目标文件 | 可见性 |
|---|---|---|
| `readStepShape()` | `cad/StepReader.cpp` | 内部实现 |
| `computeLinearDeflection()` | `mesh/ShapeMesher.cpp` | 内部 |
| `extractPoints()` | `mesh/ShapeMesher.cpp` | 内部 |
| `extractLines()` | `mesh/ShapeMesher.cpp` | 内部 |
| `extractFaces()` | `mesh/ShapeMesher.cpp` | 内部 |
| `appendPoint/LineSegment/Polyline()` | `mesh/ShapeMesher.cpp` | 内部 |
| `appendEdgeFromPolygon3D/OnTriangulation/FromCurve()` | `mesh/ShapeMesher.cpp` | 内部 |
| `BoundsAccumulator` | `mesh/ShapeMesher.cpp` | 内部 |
| `PointBuffers/LineBuffers/FaceBuffers/SceneBuffers` | `mesh/ShapeMesher.cpp` | 内部 |
| `toVec3(gp_Pnt)`, `toVec3(gp_Vec)`, `updateBounds()` | `mesh/ShapeMesher.cpp` | 内部 |
| 6 个 GLSL shader 字符串 | `scene/SceneBuilder.cpp` | 内部 |
| `createPrimitivePipeline()` | `scene/SceneBuilder.cpp` | 内部 |
| `createPositionOnlyNode()` | `scene/SceneBuilder.cpp` | 内部 |
| `createFaceNode()` | `scene/SceneBuilder.cpp` | 内部 |
| `createPrimitiveSwitch()` | `scene/SceneBuilder.cpp` | 内部 |
| `SceneNodes` | `scene/SceneBuilder.cpp` | 内部 |
| `loadStepScene()` | `StepModelLoader.cpp` | 公共 API |

## CMake 变更

`src/vsgocct/CMakeLists.txt` 更新源文件列表：

```cmake
add_library(vsgocct
    StepModelLoader.cpp
    cad/StepReader.cpp
    mesh/ShapeMesher.cpp
    scene/SceneBuilder.cpp
)
```

include 目录和链接依赖保持不变。

## 影响范围

- `StepModelLoader.h` — **不变**
- `examples/vsgqt_step_viewer/main.cpp` — **不变**
- 根 `CMakeLists.txt` — **不变**
- `src/vsgocct/CMakeLists.txt` — 更新源文件列表

## 验收标准

1. 项目可正常编译
2. 示例程序 `vsgqt_step_viewer` 行为与拆分前完全一致
3. 新的分层 API（`cad::readStep` -> `mesh::triangulate` -> `scene::buildScene`）可独立调用
4. `loadStepScene()` facade 继续正常工作
