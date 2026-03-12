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
| `StepSceneData` 位置 | 独立头文件 `StepSceneData.h` | 避免 scene 模块反向依赖 facade 头文件 |
| `InParallel` 参数 | 内部硬编码为 `true`，不暴露 | 当前无需用户控制，后续按需加入 `MeshOptions` |

## 文件结构

```
include/vsgocct/
├── StepSceneData.h            # StepSceneData 结构体（从 StepModelLoader.h 提取）
├── StepModelLoader.h          # 保留 facade API，include StepSceneData.h
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

### StepSceneData（独立头文件）

`StepSceneData` 从 `StepModelLoader.h` 提取到独立头文件 `include/vsgocct/StepSceneData.h`。
`StepModelLoader.h` 改为 `#include <vsgocct/StepSceneData.h>` 以保持向后兼容。

```cpp
// include/vsgocct/StepSceneData.h
#pragma once

#include <cstddef>
#include <vsg/all.h>

namespace vsgocct
{
struct StepSceneData
{
    // ... 内容与现有 StepModelLoader.h 中的定义完全一致 ...
};
} // namespace vsgocct
```

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
#include <vsg/maths/dvec3.h>

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
- 如果 `linearDeflection == 0`，先根据包围盒对角线自动计算
- 调用 `BRepMesh_IncrementalMesh(shape, linearDeflection, relative, angularDeflection, true)` 触发全形体三角化（`InParallel` 硬编码为 `true`）
- 按拓扑提取 Vertex（点）、Edge（线）、Face（面）
- 线提取三级回退：Polygon3D -> PolygonOnTriangulation -> 曲线采样
- 累计包围盒
- 如果三类 primitive 均无可渲染数据，`triangulate()` 抛 `std::runtime_error`

**执行顺序**（必须严格遵守）：
1. 计算或使用 `linearDeflection`
2. 调用 `BRepMesh_IncrementalMesh`（必须在提取之前完成）
3. `extractPoints()`
4. `extractLines()`（需要 `linearDeflection` 作为曲线采样回退的精度参数）
5. `extractFaces()`
6. 检查 `hasGeometry()`，无几何则抛异常

**内部实现细节（不暴露）**：
- `BoundsAccumulator`
- `PointBuffers`, `LineBuffers`, `FaceBuffers`, `SceneBuffers`
- `IndexedShapeMap` 类型别名
- `toVec3()`, `updateBounds()`, `appendPoint()`, `appendLineSegment()`, `appendPolyline()`
- `extractPoints()`, `extractLines()`, `extractFaces()`
- `extractSceneBuffers()` —— 三角化阶段的内部入口函数
- `appendEdgeFromPolygon3D()`, `appendEdgeFromPolygonOnTriangulation()`, `appendEdgeFromCurve()`
- `computeLinearDeflection()`

### scene 模块

```cpp
// include/vsgocct/scene/SceneBuilder.h
#pragma once

#include <vsgocct/StepSceneData.h>
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
- 从 `MeshResult::boundsMin/boundsMax` 计算 `center` 和 `radius`
- 从 `MeshResult` 填充 `pointCount`、`lineSegmentCount`、`triangleCount`

**内部实现细节（不暴露）**：
- 6 个 GLSL shader 字符串常量
- `createPrimitivePipeline()`
- `createPositionOnlyNode()`
- `createFaceNode()`
- `createPrimitiveSwitch()`
- `createSceneNodes()` —— 场景构建阶段的内部入口函数
- `SceneNodes` 内部结构体

### facade

```cpp
// StepModelLoader.h
#pragma once

#include <filesystem>
#include <vsgocct/StepSceneData.h>

namespace vsgocct
{
StepSceneData loadStepScene(const std::filesystem::path& stepFile);
} // namespace vsgocct
```

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

**Options 转发策略**：`loadStepScene()` 使用各模块的默认 Options。需要自定义参数的高级用户应直接调用分层 API（`cad::readStep` -> `mesh::triangulate` -> `scene::buildScene`）。facade 不提供 Options 重载，以保持向后兼容的简洁性。

## 代码搬迁映射

| 原位置（StepModelLoader.cpp） | 目标文件 | 可见性 |
|---|---|---|
| `readStepShape()` | `cad/StepReader.cpp` | 内部，被 `readStep()` 调用 |
| `computeLinearDeflection()` | `mesh/ShapeMesher.cpp` | 内部 |
| `extractSceneBuffers()` | `mesh/ShapeMesher.cpp` | 内部，被 `triangulate()` 调用 |
| `extractPoints()` | `mesh/ShapeMesher.cpp` | 内部 |
| `extractLines()` | `mesh/ShapeMesher.cpp` | 内部 |
| `extractFaces()` | `mesh/ShapeMesher.cpp` | 内部 |
| `appendPoint/LineSegment/Polyline()` | `mesh/ShapeMesher.cpp` | 内部 |
| `appendEdgeFromPolygon3D/OnTriangulation/FromCurve()` | `mesh/ShapeMesher.cpp` | 内部 |
| `BoundsAccumulator` | `mesh/ShapeMesher.cpp` | 内部 |
| `PointBuffers/LineBuffers/FaceBuffers/SceneBuffers` | `mesh/ShapeMesher.cpp` | 内部 |
| `IndexedShapeMap` 类型别名 | `mesh/ShapeMesher.cpp` | 内部 |
| `toVec3(gp_Pnt)`, `toVec3(gp_Vec)`, `updateBounds()` | `mesh/ShapeMesher.cpp` | 内部 |
| 6 个 GLSL shader 字符串 | `scene/SceneBuilder.cpp` | 内部 |
| `createPrimitivePipeline()` | `scene/SceneBuilder.cpp` | 内部 |
| `createPositionOnlyNode()` | `scene/SceneBuilder.cpp` | 内部 |
| `createFaceNode()` | `scene/SceneBuilder.cpp` | 内部 |
| `createPrimitiveSwitch()` | `scene/SceneBuilder.cpp` | 内部 |
| `createSceneNodes()` | `scene/SceneBuilder.cpp` | 内部，被 `buildScene()` 调用 |
| `SceneNodes` | `scene/SceneBuilder.cpp` | 内部 |
| `loadStepScene()` | `StepModelLoader.cpp` | 公共 facade |

## CMake 变更

`src/vsgocct/CMakeLists.txt` 更新源文件列表，保留 `STATIC`：

```cmake
add_library(vsgocct STATIC
    StepModelLoader.cpp
    cad/StepReader.cpp
    mesh/ShapeMesher.cpp
    scene/SceneBuilder.cpp
)
```

其余部分（alias、target properties、include dirs、link libs、compile features）保持不变。

## 影响范围

- `include/vsgocct/StepSceneData.h` — **新增**（从 StepModelLoader.h 提取）
- `include/vsgocct/StepModelLoader.h` — **修改**（改为 include StepSceneData.h，移除 StepSceneData 定义）
- `include/vsgocct/cad/StepReader.h` — **新增**
- `include/vsgocct/mesh/ShapeMesher.h` — **新增**
- `include/vsgocct/scene/SceneBuilder.h` — **新增**
- `src/vsgocct/StepModelLoader.cpp` — **重写**（瘦身为 facade）
- `src/vsgocct/cad/StepReader.cpp` — **新增**
- `src/vsgocct/mesh/ShapeMesher.cpp` — **新增**
- `src/vsgocct/scene/SceneBuilder.cpp` — **新增**
- `src/vsgocct/CMakeLists.txt` — **修改**（更新源文件列表）
- `examples/vsgqt_step_viewer/main.cpp` — **不变**
- 根 `CMakeLists.txt` — **不变**

## 验收标准

1. 项目可正常编译
2. 示例程序 `vsgqt_step_viewer` 行为与拆分前完全一致
3. 新的分层 API（`cad::readStep` -> `mesh::triangulate` -> `scene::buildScene`）可独立调用
4. `loadStepScene()` facade 继续正常工作
5. `StepSceneData` 可通过 `#include <vsgocct/StepSceneData.h>` 或 `#include <vsgocct/StepModelLoader.h>` 使用
