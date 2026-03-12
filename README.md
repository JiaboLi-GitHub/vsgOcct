# vsgOcct

A bridge library that loads STEP geometry with Open CASCADE Technology (OCCT), triangulates it, and builds a VulkanSceneGraph (VSG) scene for rendering.

OCCT + VSG 桥接库：读取 STEP 几何体，三角化后构建 VSG 场景用于 Vulkan 渲染。

## Features / 功能

- Read `.step` / `.stp` files via OCCT
- Triangulate B-Rep models into points, edges, and faces
- Build a ready-to-render VSG scene graph with per-primitive-type `vsg::Switch` nodes
- Toggle visibility of points, lines, and faces independently
- Compute model bounding center and radius for camera setup
- Interactive Qt viewer with toolbar toggles

---

- 通过 OCCT 读取 `.step` / `.stp` 文件
- 将 B-Rep 模型三角化为点、边、面
- 构建可直接渲染的 VSG 场景图，每种图元类型由独立的 `vsg::Switch` 控制
- 可独立切换点、线、面的显示/隐藏
- 自动计算模型包围中心和半径，便于相机初始化
- 交互式 Qt 查看器，带工具栏开关

## Architecture / 架构

The library is split into three independent modules connected by a thin facade:

库分为三个独立模块，由一个薄 facade 串联：

```
STEP file ──► cad::readStep()  ──► mesh::triangulate()  ──► scene::buildScene()  ──► StepSceneData
              ┌─────────────┐      ┌──────────────────┐      ┌──────────────────┐
              │ Read STEP   │      │ BRepMesh +        │      │ Vulkan pipelines │
              │ via OCCT    │      │ extract geometry  │      │ + Switch nodes   │
              └─────────────┘      └──────────────────┘      └──────────────────┘
```

| Module / 模块 | Namespace | Responsibility / 职责 |
|---|---|---|
| **cad** | `vsgocct::cad` | STEP file I/O / STEP 文件读取 |
| **mesh** | `vsgocct::mesh` | Triangulation & geometry extraction / 三角化与几何提取 |
| **scene** | `vsgocct::scene` | VSG scene graph construction / VSG 场景图构建 |
| **facade** | `vsgocct` | One-call convenience API / 一站式便捷 API |

## Directory Structure / 目录结构

```text
include/vsgocct/
├── StepSceneData.h            Scene data struct (shared return type)
├── StepModelLoader.h          Facade API: loadStepScene()
├── cad/StepReader.h           readStep() + ReaderOptions
├── mesh/ShapeMesher.h         triangulate() + MeshOptions + MeshResult
└── scene/SceneBuilder.h       buildScene() + SceneOptions

src/vsgocct/
├── StepModelLoader.cpp        Thin facade (~15 lines)
├── cad/StepReader.cpp         STEP reading implementation
├── mesh/ShapeMesher.cpp       Geometry extraction implementation
└── scene/SceneBuilder.cpp     VSG pipeline & scene building

examples/vsgqt_step_viewer/    Interactive Qt-based STEP viewer
```

## API

### Quick Start / 快速使用

```cpp
#include <vsgocct/StepModelLoader.h>

auto sceneData = vsgocct::loadStepScene("model.step");
// sceneData.scene   — root VSG node / 根场景节点
// sceneData.center  — bounding center / 包围中心
// sceneData.radius  — bounding radius / 包围半径
```

### Layered API / 分层 API

For fine-grained control, call each module directly:

如需精细控制，可直接调用各模块：

```cpp
#include <vsgocct/cad/StepReader.h>
#include <vsgocct/mesh/ShapeMesher.h>
#include <vsgocct/scene/SceneBuilder.h>

// Step 1: Read STEP file / 读取 STEP 文件
auto shapeData = vsgocct::cad::readStep("model.step");

// Step 2: Triangulate with custom options / 使用自定义参数三角化
vsgocct::mesh::MeshOptions meshOpts;
meshOpts.linearDeflection = 0.1;
meshOpts.angularDeflection = 0.5;
auto meshResult = vsgocct::mesh::triangulate(shapeData.shape, meshOpts);

// Step 3: Build scene with visibility options / 构建场景并设置可见性
vsgocct::scene::SceneOptions sceneOpts;
sceneOpts.facesVisible = true;
sceneOpts.linesVisible = true;
sceneOpts.pointsVisible = false;
auto sceneData = vsgocct::scene::buildScene(meshResult, sceneOpts);
```

### StepSceneData

| Field | Type | Description |
|---|---|---|
| `scene` | `vsg::ref_ptr<vsg::Node>` | Root scene node / 根场景节点 |
| `pointSwitch` | `vsg::ref_ptr<vsg::Switch>` | Points visibility switch / 点显示开关 |
| `lineSwitch` | `vsg::ref_ptr<vsg::Switch>` | Lines visibility switch / 线显示开关 |
| `faceSwitch` | `vsg::ref_ptr<vsg::Switch>` | Faces visibility switch / 面显示开关 |
| `center` | `vsg::dvec3` | Bounding center / 包围中心 |
| `radius` | `double` | Bounding radius / 包围半径 |
| `pointCount` | `std::size_t` | Number of points / 点数量 |
| `lineSegmentCount` | `std::size_t` | Number of line segments / 线段数量 |
| `triangleCount` | `std::size_t` | Number of triangles / 三角形数量 |

Toggle visibility at runtime / 运行时切换可见性：

```cpp
sceneData.setPointsVisible(false);
sceneData.setLinesVisible(true);
sceneData.setFacesVisible(true);
```

## Prerequisites / 依赖

- CMake 3.20+
- C++17 compiler (MSVC 2022 recommended on Windows)
- [VulkanSceneGraph](https://github.com/vsg-dev/VulkanSceneGraph) 1.1.2+
- [vsgQt](https://github.com/vsg-dev/vsgQt) (for examples)
- [Open CASCADE Technology](https://dev.opencascade.org/) (OCCT)
- Qt 5 or Qt 6

## Build / 构建

```powershell
cmake -S . -B build ^
  -DQT_PACKAGE_NAME=Qt6 ^
  -DQt6_DIR="C:/Qt/6.x.x/msvc2022_64/lib/cmake/Qt6" ^
  -Dvsg_DIR="C:/Program Files (x86)/vsg/lib/cmake/vsg" ^
  -DCMAKE_PREFIX_PATH="C:/Program Files (x86);C:/VulkanSDK/x.x.x.x"

cmake --build build --config Debug
```

To build only the library without examples:

仅构建库，不编译示例：

```powershell
cmake -S . -B build -DVSGOCCT_BUILD_EXAMPLES=OFF
```

## Running the Viewer / 运行查看器

```powershell
build\examples\vsgqt_step_viewer\Debug\vsgqt_step_viewer.exe [path/to/model.step]
```

- Pass a STEP file path as argument, or use the file dialog to select one.
- Use the **Points** / **Lines** / **Faces** toolbar buttons to toggle visibility.
- Status bar shows point, line segment, and triangle counts.

---

- 命令行传入 STEP 文件路径，或通过文件对话框选择。
- 使用工具栏的 **Points** / **Lines** / **Faces** 按钮切换显示。
- 状态栏显示点、线段、三角形数量。

## Roadmap / 路线图

See [ROADMAP.md](ROADMAP.md) for the long-term plan.

详见 [ROADMAP.md](ROADMAP.md)。

## License / 许可

MIT
