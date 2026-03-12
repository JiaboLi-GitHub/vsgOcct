# vsgOcct

[中文](README.md)

A bridge library that loads STEP geometry with Open CASCADE Technology (OCCT), triangulates it, and builds a VulkanSceneGraph (VSG) scene for rendering.

## Features

- Read `.step` / `.stp` files via OCCT
- Triangulate B-Rep models into points, edges, and faces
- Build a ready-to-render VSG scene graph with per-primitive-type `vsg::Switch` nodes
- Toggle visibility of points, lines, and faces independently
- Compute model bounding center and radius for camera setup
- Interactive Qt viewer with toolbar toggles

## Architecture

The library is split into three independent modules connected by a thin facade:

```
STEP file ──► cad::readStep()  ──► mesh::triangulate()  ──► scene::buildScene()  ──► StepSceneData
              ┌─────────────┐      ┌──────────────────┐      ┌──────────────────┐
              │ Read STEP   │      │ BRepMesh +        │      │ Vulkan pipelines │
              │ via OCCT    │      │ extract geometry  │      │ + Switch nodes   │
              └─────────────┘      └──────────────────┘      └──────────────────┘
```

| Module | Namespace | Responsibility |
|---|---|---|
| **cad** | `vsgocct::cad` | STEP file I/O |
| **mesh** | `vsgocct::mesh` | Triangulation & geometry extraction |
| **scene** | `vsgocct::scene` | VSG scene graph construction |
| **facade** | `vsgocct` | One-call convenience API |

## Directory Structure

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

### Quick Start

```cpp
#include <vsgocct/StepModelLoader.h>

auto sceneData = vsgocct::loadStepScene("model.step");
// sceneData.scene   — root VSG node
// sceneData.center  — bounding center
// sceneData.radius  — bounding radius
```

### Layered API

For fine-grained control, call each module directly:

```cpp
#include <vsgocct/cad/StepReader.h>
#include <vsgocct/mesh/ShapeMesher.h>
#include <vsgocct/scene/SceneBuilder.h>

// Step 1: Read STEP file
auto shapeData = vsgocct::cad::readStep("model.step");

// Step 2: Triangulate with custom options
vsgocct::mesh::MeshOptions meshOpts;
meshOpts.linearDeflection = 0.1;
meshOpts.angularDeflection = 0.5;
auto meshResult = vsgocct::mesh::triangulate(shapeData.shape, meshOpts);

// Step 3: Build scene with visibility options
vsgocct::scene::SceneOptions sceneOpts;
sceneOpts.facesVisible = true;
sceneOpts.linesVisible = true;
sceneOpts.pointsVisible = false;
auto sceneData = vsgocct::scene::buildScene(meshResult, sceneOpts);
```

### StepSceneData

| Field | Type | Description |
|---|---|---|
| `scene` | `vsg::ref_ptr<vsg::Node>` | Root scene node |
| `pointSwitch` | `vsg::ref_ptr<vsg::Switch>` | Points visibility switch |
| `lineSwitch` | `vsg::ref_ptr<vsg::Switch>` | Lines visibility switch |
| `faceSwitch` | `vsg::ref_ptr<vsg::Switch>` | Faces visibility switch |
| `center` | `vsg::dvec3` | Bounding center |
| `radius` | `double` | Bounding radius |
| `pointCount` | `std::size_t` | Number of points |
| `lineSegmentCount` | `std::size_t` | Number of line segments |
| `triangleCount` | `std::size_t` | Number of triangles |

Toggle visibility at runtime:

```cpp
sceneData.setPointsVisible(false);
sceneData.setLinesVisible(true);
sceneData.setFacesVisible(true);
```

## Prerequisites

- CMake 3.20+
- C++17 compiler (MSVC 2022 recommended on Windows)
- [VulkanSceneGraph](https://github.com/vsg-dev/VulkanSceneGraph) 1.1.2+
- [vsgQt](https://github.com/vsg-dev/vsgQt) (for examples)
- [Open CASCADE Technology](https://dev.opencascade.org/) (OCCT)
- Qt 5 or Qt 6

## Build

```powershell
cmake -S . -B build ^
  -DQT_PACKAGE_NAME=Qt6 ^
  -DQt6_DIR="C:/Qt/6.x.x/msvc2022_64/lib/cmake/Qt6" ^
  -Dvsg_DIR="C:/Program Files (x86)/vsg/lib/cmake/vsg" ^
  -DCMAKE_PREFIX_PATH="C:/Program Files (x86);C:/VulkanSDK/x.x.x.x"

cmake --build build --config Debug
```

To build only the library without examples:

```powershell
cmake -S . -B build -DVSGOCCT_BUILD_EXAMPLES=OFF
```

## Running the Viewer

```powershell
build\examples\vsgqt_step_viewer\Debug\vsgqt_step_viewer.exe [path/to/model.step]
```

- Pass a STEP file path as argument, or use the file dialog to select one.
- Use the **Points** / **Lines** / **Faces** toolbar buttons to toggle visibility.
- Status bar shows point, line segment, and triangle counts.

## Roadmap

See [ROADMAP.md](ROADMAP.md) for the long-term plan.

## License

MIT
