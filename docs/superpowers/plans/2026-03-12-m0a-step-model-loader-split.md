# M0a: StepModelLoader 职责拆分 Implementation Plan

> **For agentic workers:** REQUIRED: Use superpowers:subagent-driven-development (if subagents available) or superpowers:executing-plans to implement this plan. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Split the monolithic `StepModelLoader` (~800 lines) into three independent modules (`cad`, `mesh`, `scene`) with Options structs and a retained facade API.

**Architecture:** Extract code from the single `StepModelLoader.cpp` into three sub-namespace modules: `vsgocct::cad::readStep()` for STEP reading, `vsgocct::mesh::triangulate()` for geometry extraction, and `vsgocct::scene::buildScene()` for VSG scene construction. The existing `loadStepScene()` becomes a thin facade calling the three modules. `StepSceneData` gets its own header to avoid circular dependencies.

**Tech Stack:** C++17, OpenCASCADE (STEP parsing, triangulation), VulkanSceneGraph (rendering), CMake 3.20+

**Spec:** `docs/superpowers/specs/2026-03-12-m0a-step-model-loader-split-design.md`

**Build command:** `cmake --build D:/vsgOcct/build-ninja --config Debug`

**Configure command (if needed):** `cmake -G Ninja -B D:/vsgOcct/build-ninja -S D:/vsgOcct`

---

## Chunk 1: Extract StepSceneData and create cad module

### Task 1: Create directory structure

**Files:**
- Create directories: `include/vsgocct/cad/`, `include/vsgocct/mesh/`, `include/vsgocct/scene/`, `src/vsgocct/cad/`, `src/vsgocct/mesh/`, `src/vsgocct/scene/`

- [ ] **Step 1: Create all sub-directories**

```bash
cd D:/vsgOcct
mkdir -p include/vsgocct/cad include/vsgocct/mesh include/vsgocct/scene
mkdir -p src/vsgocct/cad src/vsgocct/mesh src/vsgocct/scene
```

- [ ] **Step 2: Verify directory structure**

```bash
find include/vsgocct -type d && find src/vsgocct -type d
```

Expected output should show all 6 new directories.

---

### Task 2: Extract StepSceneData to independent header

**Files:**
- Create: `include/vsgocct/StepSceneData.h`
- Modify: `include/vsgocct/StepModelLoader.h`

- [ ] **Step 1: Create `include/vsgocct/StepSceneData.h`**

Copy the `StepSceneData` struct definition (the entire struct including private helpers) from `include/vsgocct/StepModelLoader.h` into this new file. The file should contain:

```cpp
#pragma once

#include <cstddef>

#include <vsg/all.h>

namespace vsgocct
{
struct StepSceneData
{
    vsg::ref_ptr<vsg::Node> scene;

    vsg::ref_ptr<vsg::Switch> pointSwitch;
    vsg::ref_ptr<vsg::Switch> lineSwitch;
    vsg::ref_ptr<vsg::Switch> faceSwitch;

    vsg::dvec3 center;
    double radius = 1.0;

    std::size_t pointCount = 0;
    std::size_t lineSegmentCount = 0;
    std::size_t triangleCount = 0;

    void setPointsVisible(bool visible) const { setSwitchVisible(pointSwitch, visible); }
    void setLinesVisible(bool visible) const { setSwitchVisible(lineSwitch, visible); }
    void setFacesVisible(bool visible) const { setSwitchVisible(faceSwitch, visible); }

    bool pointsVisible() const { return isSwitchVisible(pointSwitch); }
    bool linesVisible() const { return isSwitchVisible(lineSwitch); }
    bool facesVisible() const { return isSwitchVisible(faceSwitch); }

private:
    static void setSwitchVisible(const vsg::ref_ptr<vsg::Switch>& switchNode, bool visible)
    {
        if (switchNode && !switchNode->children.empty())
        {
            switchNode->children.front().mask = vsg::boolToMask(visible);
        }
    }

    static bool isSwitchVisible(const vsg::ref_ptr<vsg::Switch>& switchNode)
    {
        return switchNode && !switchNode->children.empty() && switchNode->children.front().mask != vsg::MASK_OFF;
    }
};
} // namespace vsgocct
```

- [ ] **Step 2: Slim down `include/vsgocct/StepModelLoader.h`**

Replace the entire file content with:

```cpp
#pragma once

#include <filesystem>

#include <vsgocct/StepSceneData.h>

namespace vsgocct
{
StepSceneData loadStepScene(const std::filesystem::path& stepFile);
} // namespace vsgocct
```

- [ ] **Step 3: Build to verify no breakage**

```bash
cmake --build D:/vsgOcct/build-ninja --config Debug
```

Expected: Compiles successfully. The example still uses `StepModelLoader.h` which now includes `StepSceneData.h` — all types remain available.

- [ ] **Step 4: Commit**

```bash
cd D:/vsgOcct
git add include/vsgocct/StepSceneData.h include/vsgocct/StepModelLoader.h
git commit -m "refactor: extract StepSceneData to independent header"
```

---

### Task 3: Create cad module (StepReader)

**Files:**
- Create: `include/vsgocct/cad/StepReader.h`
- Create: `src/vsgocct/cad/StepReader.cpp`
- Modify: `src/vsgocct/StepModelLoader.cpp` (remove `readStepShape()`, call `cad::readStep()`)
- Modify: `src/vsgocct/CMakeLists.txt` (add `cad/StepReader.cpp`)

- [ ] **Step 1: Create `include/vsgocct/cad/StepReader.h`**

```cpp
#pragma once

#include <filesystem>

#include <TopoDS_Shape.hxx>

namespace vsgocct::cad
{
struct ReaderOptions
{
};

struct ShapeData
{
    TopoDS_Shape shape;
};

ShapeData readStep(const std::filesystem::path& stepFile,
                   const ReaderOptions& options = {});
} // namespace vsgocct::cad
```

- [ ] **Step 2: Create `src/vsgocct/cad/StepReader.cpp`**

Move `readStepShape()` from `StepModelLoader.cpp` here. Wrap it in the new namespace and create the public `readStep()` function:

```cpp
#include <vsgocct/cad/StepReader.h>

#include <IFSelect_ReturnStatus.hxx>
#include <STEPControl_Reader.hxx>
#include <Standard_Failure.hxx>

#include <fstream>
#include <stdexcept>
#include <string>

namespace vsgocct::cad
{
namespace
{
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
} // namespace

ShapeData readStep(const std::filesystem::path& stepFile, const ReaderOptions& /*options*/)
{
    ShapeData data;
    data.shape = readStepShape(stepFile);
    return data;
}
} // namespace vsgocct::cad
```

- [ ] **Step 3: Remove `readStepShape()` from `StepModelLoader.cpp` and use `cad::readStep()`**

In `src/vsgocct/StepModelLoader.cpp`:
1. Add `#include <vsgocct/cad/StepReader.h>` to the includes
2. Remove the `readStepShape()` function (lines 456-486 of the original file)
3. Remove the STEP-specific includes that are no longer needed: `STEPControl_Reader.hxx`, `IFSelect_ReturnStatus.hxx`, the `<fstream>` include used only by `readStepShape()`
4. In `loadStepScene()`, replace `const TopoDS_Shape shape = readStepShape(stepFile);` with:
   ```cpp
   const auto shapeData = cad::readStep(stepFile);
   const TopoDS_Shape& shape = shapeData.shape;
   ```
   Keep the rest of `loadStepScene()` unchanged for now (it still calls `extractSceneBuffers(shape)` etc.).

- [ ] **Step 4: Add `cad/StepReader.cpp` to CMakeLists.txt**

In `src/vsgocct/CMakeLists.txt`, change the source list:

```cmake
add_library(vsgocct STATIC
    StepModelLoader.cpp
    cad/StepReader.cpp
)
```

Also change `OpenCASCADE_INCLUDE_DIR` from `PRIVATE` to `PUBLIC`:

```cmake
target_include_directories(vsgocct
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/../../include
        ${OpenCASCADE_INCLUDE_DIR}
)
```

- [ ] **Step 5: Build to verify**

```bash
cmake --build D:/vsgOcct/build-ninja --config Debug
```

Expected: Compiles successfully. `readStep()` is called from `loadStepScene()`, which still handles mesh + scene internally.

- [ ] **Step 6: Commit**

```bash
cd D:/vsgOcct
git add include/vsgocct/cad/StepReader.h src/vsgocct/cad/StepReader.cpp src/vsgocct/StepModelLoader.cpp src/vsgocct/CMakeLists.txt
git commit -m "refactor: extract cad::readStep module from StepModelLoader"
```

---

## Chunk 2: Extract mesh and scene modules, wire up facade

### Task 4: Create mesh module (ShapeMesher)

**Files:**
- Create: `include/vsgocct/mesh/ShapeMesher.h`
- Create: `src/vsgocct/mesh/ShapeMesher.cpp`
- Modify: `src/vsgocct/StepModelLoader.cpp` (remove mesh functions, call `mesh::triangulate()`)
- Modify: `src/vsgocct/CMakeLists.txt` (add `mesh/ShapeMesher.cpp`)

- [ ] **Step 1: Create `include/vsgocct/mesh/ShapeMesher.h`**

```cpp
#pragma once

#include <cstddef>
#include <vector>

#include <TopoDS_Shape.hxx>
#include <vsg/maths/dvec3.h>
#include <vsg/maths/vec3.h>

namespace vsgocct::mesh
{
struct MeshOptions
{
    double linearDeflection = 0.0;
    double angularDeflection = 0.35;
    bool relative = false;
};

struct MeshResult
{
    std::vector<vsg::vec3> pointPositions;
    std::size_t pointCount = 0;

    std::vector<vsg::vec3> linePositions;
    std::size_t lineSegmentCount = 0;

    std::vector<vsg::vec3> facePositions;
    std::vector<vsg::vec3> faceNormals;
    std::size_t triangleCount = 0;

    vsg::dvec3 boundsMin;
    vsg::dvec3 boundsMax;

    bool hasGeometry() const
    {
        return pointCount > 0 || lineSegmentCount > 0 || triangleCount > 0;
    }
};

MeshResult triangulate(const TopoDS_Shape& shape,
                       const MeshOptions& options = {});
} // namespace vsgocct::mesh
```

- [ ] **Step 2: Create `src/vsgocct/mesh/ShapeMesher.cpp`**

Move all geometry extraction code from `StepModelLoader.cpp` into this file. This is the bulk of the migration (~500 lines). The file should contain:

1. All OCCT geometry includes (`BRepAdaptor_Curve.hxx`, `BRepBndLib.hxx`, `BRepMesh_IncrementalMesh.hxx`, `BRep_Tool.hxx`, `Bnd_Box.hxx`, `GCPnts_QuasiUniformDeflection.hxx`, `NCollection_IndexedMap.hxx`, `Poly_Polygon3D.hxx`, `Poly_PolygonOnTriangulation.hxx`, `Poly_Triangulation.hxx`, `Standard_Failure.hxx`, `TopAbs_Orientation.hxx`, `TopAbs_ShapeEnum.hxx`, `TopExp.hxx`, `TopLoc_Location.hxx`, `TopTools_ShapeMapHasher.hxx`, `TopoDS.hxx`, `TopoDS_Edge.hxx`, `TopoDS_Face.hxx`, `TopoDS_Vertex.hxx`, `gp_Pnt.hxx`, `gp_Trsf.hxx`, `gp_Vec.hxx`)
2. Standard includes: `<algorithm>`, `<array>`, `<cmath>`, `<limits>`, `<stdexcept>`, `<vector>`
3. In anonymous namespace: `IndexedShapeMap` type alias, `BoundsAccumulator`, `PointBuffers`, `LineBuffers`, `FaceBuffers`, `SceneBuffers`, `toVec3()`, `updateBounds()`, `appendPoint()`, `appendLineSegment()`, `appendPolyline()`, `computeLinearDeflection()`, `extractPoints()`, `appendEdgeFromPolygon3D()`, `appendEdgeFromPolygonOnTriangulation()`, `appendEdgeFromCurve()`, `extractLines()`, `extractFaces()`, `extractSceneBuffers()`
4. The public `triangulate()` function

The public function maps internal `SceneBuffers` to the public `MeshResult`:

```cpp
namespace vsgocct::mesh
{
// ... anonymous namespace with all the moved internal functions ...

MeshResult triangulate(const TopoDS_Shape& shape, const MeshOptions& options)
{
    const double linearDeflection = options.linearDeflection > 0.0
                                        ? options.linearDeflection
                                        : computeLinearDeflection(shape);
    BRepMesh_IncrementalMesh meshGenerator(shape, linearDeflection, options.relative, options.angularDeflection, true);
    (void)meshGenerator;

    SceneBuffers buffers;
    extractPoints(shape, buffers);
    extractLines(shape, linearDeflection, buffers);
    extractFaces(shape, buffers);

    if (!buffers.hasGeometry())
    {
        throw std::runtime_error("No renderable points, lines, or faces were produced from the STEP model.");
    }

    MeshResult result;
    result.pointPositions = std::move(buffers.points.positions);
    result.pointCount = buffers.points.pointCount;
    result.linePositions = std::move(buffers.lines.positions);
    result.lineSegmentCount = buffers.lines.segmentCount;
    result.facePositions = std::move(buffers.faces.positions);
    result.faceNormals = std::move(buffers.faces.normals);
    result.triangleCount = buffers.faces.triangleCount;
    result.boundsMin = buffers.bounds.min;
    result.boundsMax = buffers.bounds.max;
    return result;
}
} // namespace vsgocct::mesh
```

**Important:** When copying the internal functions, keep them exactly as they are in the original `StepModelLoader.cpp`. The only change is:
- They are now inside `namespace vsgocct::mesh { namespace { ... } }` instead of `namespace vsgocct { namespace { ... } }`
- `extractSceneBuffers()` is no longer needed as a standalone function — its body is inlined into `triangulate()` with `MeshOptions` support

- [ ] **Step 3: Remove mesh code from `StepModelLoader.cpp` and call `mesh::triangulate()`**

In `src/vsgocct/StepModelLoader.cpp`:
1. Add `#include <vsgocct/mesh/ShapeMesher.h>`
2. Remove ALL of the following from the anonymous namespace: `IndexedShapeMap`, `BoundsAccumulator`, `PointBuffers`, `LineBuffers`, `FaceBuffers`, `SceneBuffers`, `toVec3()`, `updateBounds()`, `appendPoint()`, `appendLineSegment()`, `appendPolyline()`, `computeLinearDeflection()`, `extractPoints()`, `appendEdgeFromPolygon3D()`, `appendEdgeFromPolygonOnTriangulation()`, `appendEdgeFromCurve()`, `extractLines()`, `extractFaces()`, `extractSceneBuffers()`
3. Remove the OCCT geometry includes no longer needed (all the `BRep*.hxx`, `Poly_*.hxx`, `TopExp.hxx`, `gp_*.hxx`, etc.)
4. Remove standard includes no longer needed: `<algorithm>`, `<array>`, `<cmath>`, `<limits>`
5. In `loadStepScene()`, replace `const SceneBuffers buffers = extractSceneBuffers(shape);` with:
   ```cpp
   const auto meshResult = mesh::triangulate(shapeData.shape);
   ```
6. Update `createSceneNodes()` call and the bounds/stats copying to work with `MeshResult` instead of `SceneBuffers`. **This is the tricky part** — temporarily adapt `createSceneNodes()` to accept `MeshResult` fields until Task 5 extracts it.

After this step, `StepModelLoader.cpp` should only contain: shader strings, pipeline/node creation functions, `createSceneNodes()`, and `loadStepScene()`.

- [ ] **Step 4: Add `mesh/ShapeMesher.cpp` to CMakeLists.txt**

```cmake
add_library(vsgocct STATIC
    StepModelLoader.cpp
    cad/StepReader.cpp
    mesh/ShapeMesher.cpp
)
```

- [ ] **Step 5: Build to verify**

```bash
cmake --build D:/vsgOcct/build-ninja --config Debug
```

Expected: Compiles successfully.

- [ ] **Step 6: Commit**

```bash
cd D:/vsgOcct
git add include/vsgocct/mesh/ShapeMesher.h src/vsgocct/mesh/ShapeMesher.cpp src/vsgocct/StepModelLoader.cpp src/vsgocct/CMakeLists.txt
git commit -m "refactor: extract mesh::triangulate module from StepModelLoader"
```

---

### Task 5: Create scene module (SceneBuilder)

**Files:**
- Create: `include/vsgocct/scene/SceneBuilder.h`
- Create: `src/vsgocct/scene/SceneBuilder.cpp`
- Modify: `src/vsgocct/StepModelLoader.cpp` (final slim-down to facade)
- Modify: `src/vsgocct/CMakeLists.txt` (add `scene/SceneBuilder.cpp`)

- [ ] **Step 1: Create `include/vsgocct/scene/SceneBuilder.h`**

```cpp
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

- [ ] **Step 2: Create `src/vsgocct/scene/SceneBuilder.cpp`**

Move all rendering/scene code from `StepModelLoader.cpp` into this file. This includes:

1. VSG include: `#include <vsg/all.h>` (for pipeline, shader, node types)
2. In anonymous namespace: all 6 GLSL shader string constants, `SceneNodes` struct, `createPrimitivePipeline()`, `createPositionOnlyNode()`, `createFaceNode()`, `createPrimitiveSwitch()`, `createSceneNodes()`
3. The public `buildScene()` function

Adapt `createSceneNodes()` to work with `MeshResult` fields instead of internal buffer types. Then `buildScene()` becomes:

```cpp
namespace vsgocct::scene
{
// ... anonymous namespace with shaders, pipeline, node creation functions ...

// createPrimitiveSwitch needs to accept visibility flag:
vsg::ref_ptr<vsg::Switch> createPrimitiveSwitch(const vsg::ref_ptr<vsg::Node>& node, bool visible)
{
    auto primitiveSwitch = vsg::Switch::create();
    primitiveSwitch->addChild(visible, node ? node : vsg::Group::create());
    return primitiveSwitch;
}

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
```

**Note:** `createFaceNode()` needs to be adapted to accept `const std::vector<vsg::vec3>& positions, const std::vector<vsg::vec3>& normals` instead of `const FaceBuffers& buffers`. Same for `createPositionOnlyNode()` — it already accepts `const std::vector<vsg::vec3>&`, so no change needed there.

- [ ] **Step 3: Rewrite `StepModelLoader.cpp` as facade**

Replace the entire content of `src/vsgocct/StepModelLoader.cpp` with:

```cpp
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

- [ ] **Step 4: Add `scene/SceneBuilder.cpp` to CMakeLists.txt**

Final source list:

```cmake
add_library(vsgocct STATIC
    StepModelLoader.cpp
    cad/StepReader.cpp
    mesh/ShapeMesher.cpp
    scene/SceneBuilder.cpp
)
```

- [ ] **Step 5: Build to verify**

```bash
cmake --build D:/vsgOcct/build-ninja --config Debug
```

Expected: Compiles successfully. All code is now in the correct modules.

- [ ] **Step 6: Commit**

```bash
cd D:/vsgOcct
git add include/vsgocct/scene/SceneBuilder.h src/vsgocct/scene/SceneBuilder.cpp src/vsgocct/StepModelLoader.cpp src/vsgocct/CMakeLists.txt
git commit -m "refactor: extract scene::buildScene module, complete StepModelLoader split"
```

---

### Task 6: Final verification

**Files:** None modified — verification only.

- [ ] **Step 1: Clean build**

```bash
cmake --build D:/vsgOcct/build-ninja --config Debug --clean-first
```

Expected: Full clean build succeeds with no warnings related to the refactored code.

- [ ] **Step 2: Verify file structure matches spec**

```bash
cd D:/vsgOcct
ls include/vsgocct/StepSceneData.h include/vsgocct/StepModelLoader.h include/vsgocct/cad/StepReader.h include/vsgocct/mesh/ShapeMesher.h include/vsgocct/scene/SceneBuilder.h
ls src/vsgocct/StepModelLoader.cpp src/vsgocct/cad/StepReader.cpp src/vsgocct/mesh/ShapeMesher.cpp src/vsgocct/scene/SceneBuilder.cpp
```

Expected: All 9 files exist.

- [ ] **Step 3: Verify `StepModelLoader.cpp` is a thin facade**

```bash
wc -l src/vsgocct/StepModelLoader.cpp
```

Expected: ~15 lines (includes + namespace + 3-line function body).

- [ ] **Step 4: Run the example application manually**

Launch `vsgqt_step_viewer` with a test STEP file (e.g., from `D:/OCCT/data/step/`) and verify:
- Model loads and displays correctly
- Points/Lines/Faces toggle buttons work
- Status bar shows correct counts
- Camera rotation/zoom works

This is a manual visual verification — the behavior must be identical to before the refactor.

- [ ] **Step 5: Commit verification notes (optional)**

If any adjustments were needed during verification, commit them now.
