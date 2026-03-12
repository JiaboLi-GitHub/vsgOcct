# vsgOcct

[English](README_EN.md)

OCCT + VSG 桥接库：读取 STEP 几何体，三角化后构建 VSG 场景用于 Vulkan 渲染。

## 功能

- 通过 OCCT 读取 `.step` / `.stp` 文件
- 将 B-Rep 模型三角化为点、边、面
- 构建可直接渲染的 VSG 场景图，每种图元类型由独立的 `vsg::Switch` 控制
- 可独立切换点、线、面的显示/隐藏
- 自动计算模型包围中心和半径，便于相机初始化
- 交互式 Qt 查看器，带工具栏开关

## 架构

库分为三个独立模块，由一个薄 facade 串联：

```
STEP file ──► cad::readStep()  ──► mesh::triangulate()  ──► scene::buildScene()  ──► StepSceneData
              ┌─────────────┐      ┌──────────────────┐      ┌──────────────────┐
              │ 读取 STEP   │      │ BRepMesh +        │      │ Vulkan 管线 +    │
              │ (OCCT)      │      │ 提取几何数据      │      │ Switch 节点      │
              └─────────────┘      └──────────────────┘      └──────────────────┘
```

| 模块 | 命名空间 | 职责 |
|---|---|---|
| **cad** | `vsgocct::cad` | STEP 文件读取 |
| **mesh** | `vsgocct::mesh` | 三角化与几何提取 |
| **scene** | `vsgocct::scene` | VSG 场景图构建 |
| **facade** | `vsgocct` | 一站式便捷 API |

## 目录结构

```text
include/vsgocct/
├── StepSceneData.h            场景数据结构体（共享返回类型）
├── StepModelLoader.h          Facade API: loadStepScene()
├── cad/StepReader.h           readStep() + ReaderOptions
├── mesh/ShapeMesher.h         triangulate() + MeshOptions + MeshResult
└── scene/SceneBuilder.h       buildScene() + SceneOptions

src/vsgocct/
├── StepModelLoader.cpp        薄 facade（约 15 行）
├── cad/StepReader.cpp         STEP 读取实现
├── mesh/ShapeMesher.cpp       几何提取实现
└── scene/SceneBuilder.cpp     VSG 管线与场景构建

examples/vsgqt_step_viewer/    基于 Qt 的交互式 STEP 查看器
```

## API

### 快速使用

```cpp
#include <vsgocct/StepModelLoader.h>

auto sceneData = vsgocct::loadStepScene("model.step");
// sceneData.scene   — 根场景节点
// sceneData.center  — 包围中心
// sceneData.radius  — 包围半径
```

### 分层 API

如需精细控制，可直接调用各模块：

```cpp
#include <vsgocct/cad/StepReader.h>
#include <vsgocct/mesh/ShapeMesher.h>
#include <vsgocct/scene/SceneBuilder.h>

// 第 1 步：读取 STEP 文件
auto shapeData = vsgocct::cad::readStep("model.step");

// 第 2 步：使用自定义参数三角化
vsgocct::mesh::MeshOptions meshOpts;
meshOpts.linearDeflection = 0.1;
meshOpts.angularDeflection = 0.5;
auto meshResult = vsgocct::mesh::triangulate(shapeData.shape, meshOpts);

// 第 3 步：构建场景并设置可见性
vsgocct::scene::SceneOptions sceneOpts;
sceneOpts.facesVisible = true;
sceneOpts.linesVisible = true;
sceneOpts.pointsVisible = false;
auto sceneData = vsgocct::scene::buildScene(meshResult, sceneOpts);
```

### StepSceneData

| 字段 | 类型 | 说明 |
|---|---|---|
| `scene` | `vsg::ref_ptr<vsg::Node>` | 根场景节点 |
| `pointSwitch` | `vsg::ref_ptr<vsg::Switch>` | 点显示开关 |
| `lineSwitch` | `vsg::ref_ptr<vsg::Switch>` | 线显示开关 |
| `faceSwitch` | `vsg::ref_ptr<vsg::Switch>` | 面显示开关 |
| `center` | `vsg::dvec3` | 包围中心 |
| `radius` | `double` | 包围半径 |
| `pointCount` | `std::size_t` | 点数量 |
| `lineSegmentCount` | `std::size_t` | 线段数量 |
| `triangleCount` | `std::size_t` | 三角形数量 |

运行时切换可见性：

```cpp
sceneData.setPointsVisible(false);
sceneData.setLinesVisible(true);
sceneData.setFacesVisible(true);
```

## 依赖

- CMake 3.20+
- C++17 编译器（Windows 推荐 MSVC 2022）
- [VulkanSceneGraph](https://github.com/vsg-dev/VulkanSceneGraph) 1.1.2+
- [vsgQt](https://github.com/vsg-dev/vsgQt)（示例需要）
- [Open CASCADE Technology](https://dev.opencascade.org/)（OCCT）
- Qt 5 或 Qt 6

## 构建

```powershell
cmake -S . -B build ^
  -DQT_PACKAGE_NAME=Qt6 ^
  -DQt6_DIR="C:/Qt/6.x.x/msvc2022_64/lib/cmake/Qt6" ^
  -Dvsg_DIR="C:/Program Files (x86)/vsg/lib/cmake/vsg" ^
  -DCMAKE_PREFIX_PATH="C:/Program Files (x86);C:/VulkanSDK/x.x.x.x"

cmake --build build --config Debug
```

仅构建库，不编译示例：

```powershell
cmake -S . -B build -DVSGOCCT_BUILD_EXAMPLES=OFF
```

## 运行查看器

```powershell
build\examples\vsgqt_step_viewer\Debug\vsgqt_step_viewer.exe [path/to/model.step]
```

- 命令行传入 STEP 文件路径，或通过文件对话框选择
- 使用工具栏的 **Points** / **Lines** / **Faces** 按钮切换显示
- 状态栏显示点、线段、三角形数量

## 路线图

详见 [ROADMAP.md](ROADMAP.md)。

## 许可

MIT
