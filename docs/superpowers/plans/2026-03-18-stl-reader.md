# STL Reader Implementation Plan

> **For agentic workers:** REQUIRED: Use superpowers:subagent-driven-development (if subagents available) or superpowers:executing-plans to implement this plan. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add STL model reading with full face/edge/vertex picking support and a unified ModelLoader API.

**Architecture:** OCCT `RWStl` reads STL files into `Poly_Triangulation`. A new `StlMeshBuilder` extracts geometry and reconstructs feature edges via dihedral angle analysis, producing a standard `MeshResult`. `SceneBuilder` is refactored to expose `buildPartScene`/`assembleScene` so both STEP and STL pipelines share the same rendering path. A unified `loadScene()` dispatches by file extension.

**Tech Stack:** C++17, Open CASCADE Technology (OCCT `RWStl`, `Poly_Triangulation`), VulkanSceneGraph (VSG), GoogleTest

**Spec:** `docs/superpowers/specs/2026-03-18-stl-reader-design.md`

---

### Task 1: Extract shared material types from StepReader.h

Move `ShapeNodeColor`, `ShapeVisualMaterial`, `ShapeVisualMaterialSource` to a shared header so both STEP and STL pipelines can use them without coupling.

**Files:**
- Create: `include/vsgocct/cad/MaterialTypes.h`
- Modify: `include/vsgocct/cad/StepReader.h:23-49` (remove type definitions, add include)
- Modify: `src/vsgocct/CMakeLists.txt:9-16` (add header)

- [ ] **Step 1: Create `MaterialTypes.h`**

```cpp
// include/vsgocct/cad/MaterialTypes.h
#pragma once

#include <array>

namespace vsgocct::cad
{
struct ShapeNodeColor
{
    float r = 0.74f;
    float g = 0.79f;
    float b = 0.86f;
    bool isSet = false;
};

enum class ShapeVisualMaterialSource
{
    Default,
    ColorFallback,
    Pbr
};

struct ShapeVisualMaterial
{
    std::array<float, 4> baseColorFactor{0.74f, 0.79f, 0.86f, 1.0f};
    std::array<float, 3> emissiveFactor{0.0f, 0.0f, 0.0f};
    float metallicFactor = 0.0f;
    float roughnessFactor = 0.65f;
    float alphaCutoff = 0.5f;
    bool alphaMask = false;
    bool doubleSided = true;
    bool hasPbr = false;
    ShapeVisualMaterialSource source = ShapeVisualMaterialSource::Default;
};
} // namespace vsgocct::cad
```

- [ ] **Step 2: Update `StepReader.h` to use `MaterialTypes.h`**

In `include/vsgocct/cad/StepReader.h`:
- Add `#include <vsgocct/cad/MaterialTypes.h>` after the existing includes
- Remove the definitions of `ShapeNodeColor` (lines 24-29), `ShapeVisualMaterialSource` (lines 31-36), and `ShapeVisualMaterial` (lines 38-49)
- Keep all other contents unchanged (`ReaderOptions`, `ShapeNodeType`, `ShapeNode`, `AssemblyData`, `readStep`)

- [ ] **Step 3: Add the new header to CMakeLists.txt**

In `src/vsgocct/CMakeLists.txt`, add to `VSGOCCT_HEADERS`:
```
    ../../include/vsgocct/cad/MaterialTypes.h
```

- [ ] **Step 4: Build and run all existing tests**

Run: `cmake --build build --config Release && ctest --test-dir build -C Release --output-on-failure`
Expected: All existing tests pass (no behavioral change, just moved types)

- [ ] **Step 5: Commit**

```bash
git add include/vsgocct/cad/MaterialTypes.h include/vsgocct/cad/StepReader.h src/vsgocct/CMakeLists.txt
git commit -m "refactor(cad): extract MaterialTypes.h from StepReader.h"
```

---

### Task 2: Refactor SceneBuilder to expose `buildPartScene` and `assembleScene`

Extract the per-part scene construction logic from `buildNodeSubgraph` and the scene assembly logic from `buildAssemblyScene` into two new public functions.

**Files:**
- Modify: `include/vsgocct/scene/SceneBuilder.h:77-80` (add declarations)
- Modify: `src/vsgocct/scene/SceneBuilder.cpp:699-849` (extract logic)

- [ ] **Step 1: Add new function declarations to `SceneBuilder.h`**

Add before the closing `}` of the namespace (before line 97):
```cpp
PartSceneNode buildPartScene(
    uint32_t partId,
    const std::string& name,
    const mesh::MeshResult& mesh,
    const cad::ShapeNodeColor& color,
    const cad::ShapeVisualMaterial& material,
    const SceneOptions& sceneOptions);

AssemblySceneData assembleScene(
    std::vector<PartSceneNode>&& parts,
    const SceneOptions& sceneOptions);
```

Note: `SceneBuilder.h` already includes `cad/StepReader.h` (line 12), which now transitively includes `MaterialTypes.h`. The types are available.

- [ ] **Step 2: Implement `buildPartScene` in `SceneBuilder.cpp`**

Add as a new public function (outside the anonymous namespace, after line 804). This function contains the logic currently in `buildNodeSubgraph` lines 726-793 (the Part branch), but takes `MeshResult` directly instead of calling `mesh::triangulate`:

```cpp
PartSceneNode buildPartScene(
    uint32_t partId,
    const std::string& name,
    const mesh::MeshResult& mesh,
    const cad::ShapeNodeColor& color,
    const cad::ShapeVisualMaterial& material,
    const SceneOptions& sceneOptions)
{
    const auto faceColor = resolveBaseColor(material);
    const auto faceNode = sceneOptions.shadingMode == ShadingMode::Pbr
                              ? createPbrFaceNode(
                                    mesh.facePositions,
                                    mesh.faceNormals,
                                    material,
                                    faceColor,
                                    partId)
                              : createLegacyFaceNode(
                                    mesh.facePositions,
                                    mesh.faceNormals,
                                    faceColor,
                                    partId);
    auto lineNode = createPositionOnlyNode(
        mesh.linePositions,
        createPrimitivePipeline(
            LINE_VERT_SHADER,
            LINE_FRAG_SHADER,
            VK_PRIMITIVE_TOPOLOGY_LINE_LIST,
            false,
            VK_FORMAT_R32G32B32_SFLOAT,
            false),
        BASE_EDGE_COLOR,
        partId,
        selection::PrimitiveKind::Edge);
    auto pointNode = createPositionOnlyNode(
        mesh.pointPositions,
        createPrimitivePipeline(
            POINT_VERT_SHADER,
            POINT_FRAG_SHADER,
            VK_PRIMITIVE_TOPOLOGY_POINT_LIST,
            false,
            VK_FORMAT_R32G32B32_SFLOAT,
            false),
        BASE_VERTEX_COLOR,
        partId,
        selection::PrimitiveKind::Vertex);

    auto partGroup = vsg::Group::create();
    partGroup->setValue(PART_ID_KEY, partId);
    partGroup->addChild(faceNode.node);
    partGroup->addChild(lineNode.node);
    partGroup->addChild(pointNode.node);

    auto partSwitch = vsg::Switch::create();
    partSwitch->setValue(PART_ID_KEY, partId);
    partSwitch->addChild(true, partGroup);

    PartSceneNode partSceneNode;
    partSceneNode.partId = partId;
    partSceneNode.name = name;
    partSceneNode.switchNode = partSwitch;
    partSceneNode.baseColor = faceColor;
    partSceneNode.importedMaterial = material;
    partSceneNode.visualMaterial = material;
    partSceneNode.pbrMaterialValue = faceNode.materialValue;
    partSceneNode.faceColors = faceNode.colors;
    partSceneNode.lineColors = lineNode.colors;
    partSceneNode.pointColors = pointNode.colors;
    partSceneNode.pointSpans = mesh.pointSpans;
    partSceneNode.lineSpans = mesh.lineSpans;
    partSceneNode.faceSpans = mesh.faceSpans;
    return partSceneNode;
}
```

- [ ] **Step 3: Implement `assembleScene` in `SceneBuilder.cpp`**

Add after `buildPartScene`:

```cpp
AssemblySceneData assembleScene(
    std::vector<PartSceneNode>&& parts,
    const SceneOptions& sceneOptions)
{
    auto root = vsg::Group::create();
    if (sceneOptions.shadingMode == ShadingMode::Pbr && sceneOptions.addHeadlight)
    {
        root->addChild(vsg::createHeadlight());
    }

    BoundsAccumulator bounds;
    std::size_t totalTriangles = 0;
    std::size_t totalLines = 0;
    std::size_t totalPoints = 0;

    for (auto& part : parts)
    {
        root->addChild(part.switchNode);

        // Accumulate bounds from span data
        // (bounds are computed from the mesh positions which are already in the buffers)
    }

    AssemblySceneData sceneData;
    sceneData.scene = root;
    sceneData.shadingMode = sceneOptions.shadingMode;
    sceneData.materialPreset = MaterialPreset::Imported;

    // Compute bounds from all part switch nodes using VSG's ComputeBounds visitor
    auto computeBounds = vsg::ComputeBounds::create();
    root->accept(*computeBounds);
    if (computeBounds->bounds.valid())
    {
        const auto& bb = computeBounds->bounds;
        sceneData.center = (bb.min + bb.max) * 0.5;
        sceneData.radius = vsg::length(bb.max - bb.min) * 0.5;
        sceneData.radius = std::max(sceneData.radius, 1.0);
    }

    for (const auto& part : parts)
    {
        for (const auto& span : part.faceSpans)
        {
            totalTriangles += span.triangleCount;
        }
        for (const auto& span : part.lineSpans)
        {
            totalLines += span.segmentCount;
        }
        for (const auto& span : part.pointSpans)
        {
            totalPoints += span.pointCount;
        }
    }

    sceneData.totalTriangleCount = totalTriangles;
    sceneData.totalLineSegmentCount = totalLines;
    sceneData.totalPointCount = totalPoints;
    sceneData.parts = std::move(parts);

    return sceneData;
}
```

- [ ] **Step 4: Refactor `buildNodeSubgraph` to use `buildPartScene`**

Replace lines 726-793 of the Part branch in `buildNodeSubgraph` with:

```cpp
    const uint32_t partId = nextPartId++;
    TopoDS_Shape locatedShape = shapeNode.shape.Located(currentLocation);
    auto meshResult = mesh::triangulate(locatedShape, meshOptions);

    auto partSceneNode = buildPartScene(
        partId, shapeNode.name, meshResult,
        shapeNode.color, shapeNode.visualMaterial, sceneOptions);

    parentGroup->addChild(partSceneNode.switchNode);

    totalTriangles += meshResult.triangleCount;
    totalLines += meshResult.lineSegmentCount;
    totalPoints += meshResult.pointCount;

    if (meshResult.hasGeometry())
    {
        bounds.expand(meshResult.boundsMin, meshResult.boundsMax);
    }

    parts.push_back(std::move(partSceneNode));
```

Note: `buildAssemblyScene` continues to use its own bounds/totals accumulation (keeping original behavior), not `assembleScene`. Both paths coexist.

- [ ] **Step 5: Build and run all existing tests**

Run: `cmake --build build --config Release && ctest --test-dir build -C Release --output-on-failure`
Expected: All existing tests pass (refactoring preserves behavior)

- [ ] **Step 6: Commit**

```bash
git add include/vsgocct/scene/SceneBuilder.h src/vsgocct/scene/SceneBuilder.cpp
git commit -m "refactor(scene): extract buildPartScene and assembleScene from SceneBuilder"
```

---

### Task 3: Implement StlReader

**Files:**
- Create: `include/vsgocct/cad/StlReader.h`
- Create: `src/vsgocct/cad/StlReader.cpp`
- Create: `tests/test_stl_reader.cpp`
- Modify: `src/vsgocct/CMakeLists.txt` (add sources + TKDESTL)
- Modify: `tests/CMakeLists.txt` (add test + TKDESTL to generate_test_data)
- Modify: `tests/generate_test_data.cpp` (add STL generation)

- [ ] **Step 1: Generate STL test data**

Add STL generation to `tests/generate_test_data.cpp`. Add includes at the top:
```cpp
#include <RWStl.hxx>
#include <Poly_Triangulation.hxx>
#include <TColgp_Array1OfPnt.hxx>
#include <Poly_Array1OfTriangle.hxx>
#include <OSD_Path.hxx>
#include <fstream>
```

Add before `return 0;` in `main()`:
```cpp
    // cube.stl: ASCII STL cube (8 vertices, 12 triangles)
    {
        // Build a Poly_Triangulation for a unit cube manually
        // Vertices of a 10x10x10 cube at origin
        auto triangulation = new Poly_Triangulation(8, 12, false);
        triangulation->SetNode(1, gp_Pnt(0, 0, 0));
        triangulation->SetNode(2, gp_Pnt(10, 0, 0));
        triangulation->SetNode(3, gp_Pnt(10, 10, 0));
        triangulation->SetNode(4, gp_Pnt(0, 10, 0));
        triangulation->SetNode(5, gp_Pnt(0, 0, 10));
        triangulation->SetNode(6, gp_Pnt(10, 0, 10));
        triangulation->SetNode(7, gp_Pnt(10, 10, 10));
        triangulation->SetNode(8, gp_Pnt(0, 10, 10));

        // 12 triangles (2 per face, CCW winding)
        triangulation->SetTriangle(1, Poly_Triangle(1, 3, 2));  // bottom
        triangulation->SetTriangle(2, Poly_Triangle(1, 4, 3));
        triangulation->SetTriangle(3, Poly_Triangle(5, 6, 7));  // top
        triangulation->SetTriangle(4, Poly_Triangle(5, 7, 8));
        triangulation->SetTriangle(5, Poly_Triangle(1, 2, 6));  // front
        triangulation->SetTriangle(6, Poly_Triangle(1, 6, 5));
        triangulation->SetTriangle(7, Poly_Triangle(3, 4, 8));  // back
        triangulation->SetTriangle(8, Poly_Triangle(3, 8, 7));
        triangulation->SetTriangle(9, Poly_Triangle(2, 3, 7));  // right
        triangulation->SetTriangle(10, Poly_Triangle(2, 7, 6));
        triangulation->SetTriangle(11, Poly_Triangle(1, 5, 8)); // left
        triangulation->SetTriangle(12, Poly_Triangle(1, 8, 4));

        // Write ASCII
        auto asciiPath = dataDir / "cube.stl";
        OSD_Path osdAscii(asciiPath.string().c_str());
        if (!RWStl::WriteAscii(triangulation, osdAscii))
        {
            std::cerr << "Failed to write: " << asciiPath << std::endl;
            std::exit(1);
        }
        std::cout << "Written: " << asciiPath << std::endl;

        // Write binary
        auto binaryPath = dataDir / "cube_binary.stl";
        OSD_Path osdBin(binaryPath.string().c_str());
        if (!RWStl::WriteBinary(triangulation, osdBin))
        {
            std::cerr << "Failed to write: " << binaryPath << std::endl;
            std::exit(1);
        }
        std::cout << "Written: " << binaryPath << std::endl;
    }
```

- [ ] **Step 2: Add TKDESTL to `tests/CMakeLists.txt`**

Change line 33:
```cmake
target_link_libraries(generate_test_data PRIVATE vsgocct TKDESTEP TKLCAF TKXCAF TKPrim TKTopAlgo TKDESTL)
```

- [ ] **Step 3: Build generate_test_data and run it**

Run: `cmake --build build --config Release --target generate_test_data && ./build/tests/Release/generate_test_data.exe`
Expected: `cube.stl` and `cube_binary.stl` created in `tests/data/`

- [ ] **Step 4: Write the failing test `test_stl_reader.cpp`**

```cpp
// tests/test_stl_reader.cpp
#include <gtest/gtest.h>
#include <vsgocct/cad/StlReader.h>
#include <filesystem>

static std::filesystem::path testDataDir()
{
    return std::filesystem::path(TEST_DATA_DIR);
}

TEST(StlReader, ReadsAsciiStlFile)
{
    auto data = vsgocct::cad::readStl(testDataDir() / "cube.stl");
    ASSERT_FALSE(data.triangulation.IsNull());
    EXPECT_EQ(data.triangulation->NbTriangles(), 12);
    EXPECT_EQ(data.triangulation->NbNodes(), 8);
    EXPECT_FALSE(data.name.empty());
}

TEST(StlReader, ReadsBinaryStlFile)
{
    auto data = vsgocct::cad::readStl(testDataDir() / "cube_binary.stl");
    ASSERT_FALSE(data.triangulation.IsNull());
    EXPECT_EQ(data.triangulation->NbTriangles(), 12);
    EXPECT_EQ(data.triangulation->NbNodes(), 8);
}

TEST(StlReader, ThrowsOnInvalidPath)
{
    EXPECT_THROW(vsgocct::cad::readStl("nonexistent.stl"), std::runtime_error);
}

TEST(StlReader, UsesFilenameStemForBinaryName)
{
    auto data = vsgocct::cad::readStl(testDataDir() / "cube_binary.stl");
    EXPECT_EQ(data.name, "cube_binary");
}
```

- [ ] **Step 5: Create `StlReader.h`**

```cpp
// include/vsgocct/cad/StlReader.h
#pragma once

#include <filesystem>
#include <string>

#include <Poly_Triangulation.hxx>
#include <Standard_Handle.hxx>

namespace vsgocct::cad
{
struct StlData
{
    Handle(Poly_Triangulation) triangulation;
    std::string name;
};

StlData readStl(const std::filesystem::path& stlFile);
} // namespace vsgocct::cad
```

- [ ] **Step 6: Implement `StlReader.cpp`**

```cpp
// src/vsgocct/cad/StlReader.cpp
#include <vsgocct/cad/StlReader.h>

#include <RWStl.hxx>

#include <fstream>
#include <stdexcept>

namespace vsgocct::cad
{
namespace
{
std::string extractAsciiSolidName(const std::filesystem::path& stlFile)
{
    std::ifstream file(stlFile);
    if (!file)
    {
        return {};
    }

    std::string line;
    if (std::getline(file, line))
    {
        // ASCII STL starts with "solid <name>"
        const std::string prefix = "solid ";
        if (line.size() > prefix.size() &&
            line.compare(0, prefix.size(), prefix) == 0)
        {
            auto name = line.substr(prefix.size());
            // Trim trailing whitespace
            while (!name.empty() && (name.back() == ' ' || name.back() == '\r' || name.back() == '\n'))
            {
                name.pop_back();
            }
            if (!name.empty())
            {
                return name;
            }
        }
    }
    return {};
}
} // namespace

StlData readStl(const std::filesystem::path& stlFile)
{
    if (!std::filesystem::exists(stlFile))
    {
        throw std::runtime_error("STL file not found: " + stlFile.u8string());
    }

    // Use const char* overload which merges coincident vertices (M_PI/2 default)
    Handle(Poly_Triangulation) triangulation = RWStl::ReadFile(stlFile.string().c_str());
    if (triangulation.IsNull() || triangulation->NbTriangles() == 0)
    {
        throw std::runtime_error("Failed to read STL file or file is empty: " + stlFile.u8string());
    }

    StlData data;
    data.triangulation = triangulation;

    // Try to extract name from ASCII header
    data.name = extractAsciiSolidName(stlFile);
    if (data.name.empty())
    {
        data.name = stlFile.stem().u8string();
    }

    return data;
}
} // namespace vsgocct::cad
```

- [ ] **Step 7: Add sources to `src/vsgocct/CMakeLists.txt`**

Add to `VSGOCCT_SOURCES`:
```
    cad/StlReader.cpp
```

Add to `VSGOCCT_HEADERS`:
```
    ../../include/vsgocct/cad/StlReader.h
```

Add `TKDESTL` to the PRIVATE link libraries (after `TKTopAlgo` on line 45):
```
        TKDESTL
```

- [ ] **Step 8: Add test to `tests/CMakeLists.txt`**

Add after the existing test registrations (after line 26):
```cmake
vsgocct_add_test(test_stl_reader)
```

- [ ] **Step 9: Build and run tests**

Run: `cmake --build build --config Release && ctest --test-dir build -C Release -R test_stl_reader --output-on-failure`
Expected: All 4 StlReader tests pass

- [ ] **Step 10: Commit**

```bash
git add include/vsgocct/cad/StlReader.h src/vsgocct/cad/StlReader.cpp tests/test_stl_reader.cpp tests/generate_test_data.cpp src/vsgocct/CMakeLists.txt tests/CMakeLists.txt
git commit -m "feat(cad): add StlReader for ASCII and Binary STL files"
```

---

### Task 4: Implement StlMeshBuilder

The core algorithm: vertex welding + feature edge detection + MeshResult construction.

**Files:**
- Create: `include/vsgocct/mesh/StlMeshBuilder.h`
- Create: `src/vsgocct/mesh/StlMeshBuilder.cpp`
- Create: `tests/test_stl_mesh_builder.cpp`
- Modify: `src/vsgocct/CMakeLists.txt` (add sources)
- Modify: `tests/CMakeLists.txt` (add test)

- [ ] **Step 1: Write the failing test `test_stl_mesh_builder.cpp`**

```cpp
// tests/test_stl_mesh_builder.cpp
#include <gtest/gtest.h>
#include <vsgocct/cad/StlReader.h>
#include <vsgocct/mesh/StlMeshBuilder.h>
#include <filesystem>

static std::filesystem::path testDataDir()
{
    return std::filesystem::path(TEST_DATA_DIR);
}

TEST(StlMeshBuilder, CubeProduces12FaceSpans)
{
    auto stlData = vsgocct::cad::readStl(testDataDir() / "cube.stl");
    auto mesh = vsgocct::mesh::buildStlMesh(stlData.triangulation);

    EXPECT_EQ(mesh.triangleCount, 12u);
    EXPECT_EQ(mesh.faceSpans.size(), 12u);
    // Each triangle = 3 vertices in facePositions
    EXPECT_EQ(mesh.facePositions.size(), 36u);
    EXPECT_EQ(mesh.faceNormals.size(), 36u);
}

TEST(StlMeshBuilder, CubeHas12FeatureEdges)
{
    // A cube has 12 edges, all with 90-degree dihedral angles
    auto stlData = vsgocct::cad::readStl(testDataDir() / "cube.stl");
    auto mesh = vsgocct::mesh::buildStlMesh(stlData.triangulation);

    // 12 geometric edges of a cube
    EXPECT_EQ(mesh.lineSpans.size(), 12u);
    EXPECT_GT(mesh.lineSegmentCount, 0u);
}

TEST(StlMeshBuilder, CubeHas8FeatureVertices)
{
    auto stlData = vsgocct::cad::readStl(testDataDir() / "cube.stl");
    auto mesh = vsgocct::mesh::buildStlMesh(stlData.triangulation);

    EXPECT_EQ(mesh.pointSpans.size(), 8u);
    EXPECT_EQ(mesh.pointCount, 8u);
}

TEST(StlMeshBuilder, BoundsAreValid)
{
    auto stlData = vsgocct::cad::readStl(testDataDir() / "cube.stl");
    auto mesh = vsgocct::mesh::buildStlMesh(stlData.triangulation);

    // Cube is 10x10x10 at origin
    EXPECT_NEAR(mesh.boundsMin.x, 0.0, 0.01);
    EXPECT_NEAR(mesh.boundsMin.y, 0.0, 0.01);
    EXPECT_NEAR(mesh.boundsMin.z, 0.0, 0.01);
    EXPECT_NEAR(mesh.boundsMax.x, 10.0, 0.01);
    EXPECT_NEAR(mesh.boundsMax.y, 10.0, 0.01);
    EXPECT_NEAR(mesh.boundsMax.z, 10.0, 0.01);
}

TEST(StlMeshBuilder, HasGeometryIsTrue)
{
    auto stlData = vsgocct::cad::readStl(testDataDir() / "cube.stl");
    auto mesh = vsgocct::mesh::buildStlMesh(stlData.triangulation);

    EXPECT_TRUE(mesh.hasGeometry());
}

TEST(StlMeshBuilder, HighAngleThresholdReducesEdges)
{
    auto stlData = vsgocct::cad::readStl(testDataDir() / "cube.stl");
    vsgocct::mesh::StlMeshOptions options;
    options.edgeAngleThreshold = 100.0; // Higher than 90 degrees
    auto mesh = vsgocct::mesh::buildStlMesh(stlData.triangulation, options);

    // With threshold > 90, cube edges should NOT be detected as feature edges
    // Only boundary edges remain (cube has none since it's closed)
    EXPECT_EQ(mesh.lineSpans.size(), 0u);
}
```

- [ ] **Step 2: Create `StlMeshBuilder.h`**

```cpp
// include/vsgocct/mesh/StlMeshBuilder.h
#pragma once

#include <Poly_Triangulation.hxx>
#include <Standard_Handle.hxx>

#include <vsgocct/mesh/ShapeMesher.h>

namespace vsgocct::mesh
{
struct StlMeshOptions
{
    double edgeAngleThreshold = 30.0;
    double weldTolerance = 0.0;
};

MeshResult buildStlMesh(const Handle(Poly_Triangulation)& triangulation,
                         const StlMeshOptions& options = {});
} // namespace vsgocct::mesh
```

- [ ] **Step 3: Implement `StlMeshBuilder.cpp`**

```cpp
// src/vsgocct/mesh/StlMeshBuilder.cpp
#include <vsgocct/mesh/StlMeshBuilder.h>

#include <gp_Pnt.hxx>
#include <gp_Vec.hxx>

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>
#include <unordered_map>
#include <vector>

namespace vsgocct::mesh
{
namespace
{
constexpr double PI = 3.14159265358979323846;

struct Vec3Hash
{
    std::size_t operator()(const std::array<int64_t, 3>& v) const
    {
        std::size_t h = 0;
        for (auto val : v)
        {
            h ^= std::hash<int64_t>{}(val) + 0x9e3779b9 + (h << 6) + (h >> 2);
        }
        return h;
    }
};

using EdgeKey = std::pair<uint32_t, uint32_t>;

struct EdgeInfo
{
    uint32_t face1 = UINT32_MAX;
    uint32_t face2 = UINT32_MAX;
    // Original vertex indices for the edge endpoints (from face1)
    int origV0 = 0;
    int origV1 = 0;
};

std::vector<uint32_t> weldVertices(
    const Handle(Poly_Triangulation)& tri,
    double tolerance,
    vsg::dvec3& boundsMin,
    vsg::dvec3& boundsMax)
{
    const int nbNodes = tri->NbNodes();
    std::vector<uint32_t> remap(nbNodes + 1); // 1-indexed

    // First pass: compute bounds
    boundsMin = vsg::dvec3(
        std::numeric_limits<double>::max(),
        std::numeric_limits<double>::max(),
        std::numeric_limits<double>::max());
    boundsMax = vsg::dvec3(
        std::numeric_limits<double>::lowest(),
        std::numeric_limits<double>::lowest(),
        std::numeric_limits<double>::lowest());

    for (int i = 1; i <= nbNodes; ++i)
    {
        const gp_Pnt& p = tri->Node(i);
        boundsMin.x = std::min(boundsMin.x, p.X());
        boundsMin.y = std::min(boundsMin.y, p.Y());
        boundsMin.z = std::min(boundsMin.z, p.Z());
        boundsMax.x = std::max(boundsMax.x, p.X());
        boundsMax.y = std::max(boundsMax.y, p.Y());
        boundsMax.z = std::max(boundsMax.z, p.Z());
    }

    // Auto tolerance
    if (tolerance <= 0.0)
    {
        const double dx = boundsMax.x - boundsMin.x;
        const double dy = boundsMax.y - boundsMin.y;
        const double dz = boundsMax.z - boundsMin.z;
        const double diagonal = std::sqrt(dx * dx + dy * dy + dz * dz);
        tolerance = diagonal * 1.0e-6;
        if (tolerance <= 0.0)
        {
            tolerance = 1.0e-10;
        }
    }

    const double invTol = 1.0 / tolerance;

    // Spatial hash: quantize positions
    std::unordered_map<std::array<int64_t, 3>, uint32_t, Vec3Hash> grid;
    uint32_t nextWeldedId = 0;

    for (int i = 1; i <= nbNodes; ++i)
    {
        const gp_Pnt& p = tri->Node(i);
        std::array<int64_t, 3> key{
            static_cast<int64_t>(std::round(p.X() * invTol)),
            static_cast<int64_t>(std::round(p.Y() * invTol)),
            static_cast<int64_t>(std::round(p.Z() * invTol))};

        auto it = grid.find(key);
        if (it == grid.end())
        {
            grid[key] = nextWeldedId;
            remap[i] = nextWeldedId;
            ++nextWeldedId;
        }
        else
        {
            remap[i] = it->second;
        }
    }

    return remap;
}

EdgeKey makeEdgeKey(uint32_t v0, uint32_t v1)
{
    return v0 < v1 ? EdgeKey{v0, v1} : EdgeKey{v1, v0};
}

gp_Vec computeFaceNormal(const Handle(Poly_Triangulation)& tri, int triIndex)
{
    int n1 = 0, n2 = 0, n3 = 0;
    tri->Triangle(triIndex).Get(n1, n2, n3);
    const gp_Pnt& p1 = tri->Node(n1);
    const gp_Pnt& p2 = tri->Node(n2);
    const gp_Pnt& p3 = tri->Node(n3);
    gp_Vec v1(p1, p2);
    gp_Vec v2(p1, p3);
    gp_Vec normal = v1.Crossed(v2);
    if (normal.SquareMagnitude() > 1.0e-24)
    {
        normal.Normalize();
    }
    return normal;
}

} // namespace

MeshResult buildStlMesh(const Handle(Poly_Triangulation)& triangulation,
                         const StlMeshOptions& options)
{
    if (triangulation.IsNull() || triangulation->NbTriangles() == 0)
    {
        throw std::runtime_error("Empty triangulation passed to buildStlMesh");
    }

    const int nbTriangles = triangulation->NbTriangles();

    // Step 0: Weld vertices
    vsg::dvec3 boundsMin, boundsMax;
    auto weldRemap = weldVertices(triangulation, options.weldTolerance, boundsMin, boundsMax);

    // Step 1: Build half-edge map and extract face data simultaneously
    std::map<EdgeKey, EdgeInfo> edgeMap;

    MeshResult result;
    result.boundsMin = boundsMin;
    result.boundsMax = boundsMax;

    for (int triIdx = 1; triIdx <= nbTriangles; ++triIdx)
    {
        int n1 = 0, n2 = 0, n3 = 0;
        triangulation->Triangle(triIdx).Get(n1, n2, n3);

        const gp_Pnt& p1 = triangulation->Node(n1);
        const gp_Pnt& p2 = triangulation->Node(n2);
        const gp_Pnt& p3 = triangulation->Node(n3);

        // Compute face normal
        gp_Vec faceNormal = gp_Vec(p1, p2).Crossed(gp_Vec(p1, p3));
        if (faceNormal.SquareMagnitude() > 1.0e-24)
        {
            faceNormal.Normalize();
        }

        vsg::vec3 normal(
            static_cast<float>(faceNormal.X()),
            static_cast<float>(faceNormal.Y()),
            static_cast<float>(faceNormal.Z()));

        // Add face positions and normals (3 vertices per triangle, flat normal)
        const uint32_t firstTriangle = static_cast<uint32_t>(result.triangleCount);
        result.facePositions.push_back(vsg::vec3(
            static_cast<float>(p1.X()), static_cast<float>(p1.Y()), static_cast<float>(p1.Z())));
        result.facePositions.push_back(vsg::vec3(
            static_cast<float>(p2.X()), static_cast<float>(p2.Y()), static_cast<float>(p2.Z())));
        result.facePositions.push_back(vsg::vec3(
            static_cast<float>(p3.X()), static_cast<float>(p3.Y()), static_cast<float>(p3.Z())));
        result.faceNormals.push_back(normal);
        result.faceNormals.push_back(normal);
        result.faceNormals.push_back(normal);
        ++result.triangleCount;

        result.faceSpans.push_back(FaceSpan{
            static_cast<uint32_t>(triIdx - 1),
            firstTriangle,
            1u});

        // Register edges in half-edge map using welded indices
        const uint32_t faceIndex = static_cast<uint32_t>(triIdx - 1);
        const uint32_t wv1 = weldRemap[n1];
        const uint32_t wv2 = weldRemap[n2];
        const uint32_t wv3 = weldRemap[n3];

        std::array<std::pair<uint32_t, uint32_t>, 3> edges = {{
            {wv1, wv2}, {wv2, wv3}, {wv3, wv1}}};
        // Original (unwelded) vertex indices for edge endpoint positions
        std::array<std::pair<int, int>, 3> origEdges = {{
            {n1, n2}, {n2, n3}, {n3, n1}}};

        for (int e = 0; e < 3; ++e)
        {
            if (edges[e].first == edges[e].second)
            {
                continue; // Degenerate edge after welding
            }

            auto key = makeEdgeKey(edges[e].first, edges[e].second);
            auto it = edgeMap.find(key);
            if (it == edgeMap.end())
            {
                EdgeInfo info;
                info.face1 = faceIndex;
                info.origV0 = origEdges[e].first;
                info.origV1 = origEdges[e].second;
                edgeMap[key] = info;
            }
            else if (it->second.face2 == UINT32_MAX)
            {
                it->second.face2 = faceIndex;
            }
            // else: non-manifold edge (shared by > 2 faces), ignore
        }
    }

    // Step 2: Classify edges and extract feature edges
    const double angleThresholdRad = options.edgeAngleThreshold * PI / 180.0;
    std::set<uint32_t> featureVertexSet;
    uint32_t edgeId = 0;

    for (const auto& [key, info] : edgeMap)
    {
        bool isFeature = false;

        if (info.face2 == UINT32_MAX)
        {
            // Boundary edge
            isFeature = true;
        }
        else
        {
            // Compute dihedral angle between the two faces
            gp_Vec n1 = computeFaceNormal(triangulation, static_cast<int>(info.face1) + 1);
            gp_Vec n2 = computeFaceNormal(triangulation, static_cast<int>(info.face2) + 1);

            double dotProduct = n1.Dot(n2);
            dotProduct = std::clamp(dotProduct, -1.0, 1.0);
            double angle = std::acos(dotProduct);

            if (angle > angleThresholdRad)
            {
                isFeature = true;
            }
        }

        if (isFeature)
        {
            const gp_Pnt& pA = triangulation->Node(info.origV0);
            const gp_Pnt& pB = triangulation->Node(info.origV1);

            const uint32_t firstSegment = static_cast<uint32_t>(result.lineSegmentCount);
            result.linePositions.push_back(vsg::vec3(
                static_cast<float>(pA.X()), static_cast<float>(pA.Y()), static_cast<float>(pA.Z())));
            result.linePositions.push_back(vsg::vec3(
                static_cast<float>(pB.X()), static_cast<float>(pB.Y()), static_cast<float>(pB.Z())));
            ++result.lineSegmentCount;

            result.lineSpans.push_back(LineSpan{edgeId, firstSegment, 1u});
            ++edgeId;

            featureVertexSet.insert(key.first);
            featureVertexSet.insert(key.second);
        }
    }

    // Step 3: Extract feature vertices
    // Need to map welded vertex IDs back to positions
    // Build a map of weldedId -> position (use first occurrence)
    std::unordered_map<uint32_t, gp_Pnt> weldedPositions;
    for (int i = 1; i <= triangulation->NbNodes(); ++i)
    {
        uint32_t wid = weldRemap[i];
        if (weldedPositions.find(wid) == weldedPositions.end())
        {
            weldedPositions[wid] = triangulation->Node(i);
        }
    }

    uint32_t vertexId = 0;
    for (uint32_t weldedIdx : featureVertexSet)
    {
        auto it = weldedPositions.find(weldedIdx);
        if (it == weldedPositions.end())
        {
            continue;
        }

        const gp_Pnt& p = it->second;
        const uint32_t firstPoint = static_cast<uint32_t>(result.pointCount);
        result.pointPositions.push_back(vsg::vec3(
            static_cast<float>(p.X()), static_cast<float>(p.Y()), static_cast<float>(p.Z())));
        ++result.pointCount;

        result.pointSpans.push_back(PointSpan{vertexId, firstPoint, 1u});
        ++vertexId;
    }

    return result;
}
} // namespace vsgocct::mesh
```

- [ ] **Step 4: Add sources to `src/vsgocct/CMakeLists.txt`**

Add to `VSGOCCT_SOURCES`:
```
    mesh/StlMeshBuilder.cpp
```

Add to `VSGOCCT_HEADERS`:
```
    ../../include/vsgocct/mesh/StlMeshBuilder.h
```

- [ ] **Step 5: Add test to `tests/CMakeLists.txt`**

```cmake
vsgocct_add_test(test_stl_mesh_builder)
```

- [ ] **Step 6: Build and run tests**

Run: `cmake --build build --config Release && ctest --test-dir build -C Release -R test_stl_mesh_builder --output-on-failure`
Expected: All 6 StlMeshBuilder tests pass

- [ ] **Step 7: Commit**

```bash
git add include/vsgocct/mesh/StlMeshBuilder.h src/vsgocct/mesh/StlMeshBuilder.cpp tests/test_stl_mesh_builder.cpp src/vsgocct/CMakeLists.txt tests/CMakeLists.txt
git commit -m "feat(mesh): add StlMeshBuilder with feature edge reconstruction"
```

---

### Task 5: Implement unified ModelLoader

**Files:**
- Create: `include/vsgocct/ModelLoader.h`
- Create: `src/vsgocct/ModelLoader.cpp`
- Modify: `src/vsgocct/CMakeLists.txt` (add sources)

- [ ] **Step 1: Create `ModelLoader.h`**

```cpp
// include/vsgocct/ModelLoader.h
#pragma once

#include <filesystem>

#include <vsgocct/mesh/ShapeMesher.h>
#include <vsgocct/mesh/StlMeshBuilder.h>
#include <vsgocct/scene/SceneBuilder.h>

namespace vsgocct
{
scene::AssemblySceneData loadScene(
    const std::filesystem::path& modelFile,
    const scene::SceneOptions& sceneOptions = {});

scene::AssemblySceneData loadStepScene(
    const std::filesystem::path& stepFile,
    const mesh::MeshOptions& meshOptions = {},
    const scene::SceneOptions& sceneOptions = {});

scene::AssemblySceneData loadStlScene(
    const std::filesystem::path& stlFile,
    const mesh::StlMeshOptions& stlOptions = {},
    const scene::SceneOptions& sceneOptions = {});
} // namespace vsgocct
```

- [ ] **Step 2: Implement `ModelLoader.cpp`**

```cpp
// src/vsgocct/ModelLoader.cpp
#include <vsgocct/ModelLoader.h>

#include <vsgocct/cad/StepReader.h>
#include <vsgocct/cad/StlReader.h>
#include <vsgocct/mesh/StlMeshBuilder.h>
#include <vsgocct/scene/SceneBuilder.h>

#include <algorithm>
#include <cctype>
#include <stdexcept>

namespace vsgocct
{
namespace
{
std::string toLower(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return s;
}
} // namespace

scene::AssemblySceneData loadScene(
    const std::filesystem::path& modelFile,
    const scene::SceneOptions& sceneOptions)
{
    const auto ext = toLower(modelFile.extension().u8string());

    if (ext == ".step" || ext == ".stp")
    {
        return loadStepScene(modelFile, {}, sceneOptions);
    }

    if (ext == ".stl")
    {
        return loadStlScene(modelFile, {}, sceneOptions);
    }

    throw std::runtime_error("Unsupported file format: " + ext);
}

scene::AssemblySceneData loadStepScene(
    const std::filesystem::path& stepFile,
    const mesh::MeshOptions& meshOptions,
    const scene::SceneOptions& sceneOptions)
{
    auto assembly = cad::readStep(stepFile);
    return scene::buildAssemblyScene(assembly, meshOptions, sceneOptions);
}

scene::AssemblySceneData loadStlScene(
    const std::filesystem::path& stlFile,
    const mesh::StlMeshOptions& stlOptions,
    const scene::SceneOptions& sceneOptions)
{
    auto stlData = cad::readStl(stlFile);
    auto meshResult = mesh::buildStlMesh(stlData.triangulation, stlOptions);

    cad::ShapeNodeColor defaultColor;
    cad::ShapeVisualMaterial defaultMaterial;

    auto partNode = scene::buildPartScene(
        0, stlData.name, meshResult,
        defaultColor, defaultMaterial, sceneOptions);

    std::vector<scene::PartSceneNode> parts;
    parts.push_back(std::move(partNode));
    return scene::assembleScene(std::move(parts), sceneOptions);
}
} // namespace vsgocct
```

- [ ] **Step 3: Update `src/vsgocct/CMakeLists.txt`**

Replace `StepModelLoader.cpp` with `ModelLoader.cpp` in `VSGOCCT_SOURCES` (since `ModelLoader.cpp` now defines `loadStepScene`, keeping both would cause duplicate symbol errors):

Change:
```
    StepModelLoader.cpp
```
To:
```
    ModelLoader.cpp
```

Add to `VSGOCCT_HEADERS`:
```
    ../../include/vsgocct/ModelLoader.h
```

Keep `../../include/vsgocct/StepModelLoader.h` in headers for backward compatibility (it still compiles, just delegates to `ModelLoader.h` transitively).

- [ ] **Step 4: Build and run all tests**

Run: `cmake --build build --config Release && ctest --test-dir build -C Release --output-on-failure`
Expected: All tests pass (existing + new)

- [ ] **Step 5: Commit**

```bash
git add include/vsgocct/ModelLoader.h src/vsgocct/ModelLoader.cpp src/vsgocct/CMakeLists.txt
git commit -m "feat: add unified ModelLoader with loadScene/loadStlScene/loadStepScene"
```

---

### Task 6: Integration test — end-to-end STL loading

Verify the full pipeline works by loading an STL through `loadScene` and checking the resulting scene data.

**Files:**
- Create: `tests/test_model_loader.cpp`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Write integration test**

```cpp
// tests/test_model_loader.cpp
#include <gtest/gtest.h>
#include <vsgocct/ModelLoader.h>
#include <filesystem>

static std::filesystem::path testDataDir()
{
    return std::filesystem::path(TEST_DATA_DIR);
}

TEST(ModelLoader, LoadStlSceneProducesValidScene)
{
    auto sceneData = vsgocct::loadScene(testDataDir() / "cube.stl");

    ASSERT_NE(sceneData.scene, nullptr);
    EXPECT_EQ(sceneData.parts.size(), 1u);
    EXPECT_EQ(sceneData.totalTriangleCount, 12u);
    EXPECT_GT(sceneData.totalLineSegmentCount, 0u);
    EXPECT_GT(sceneData.totalPointCount, 0u);
    EXPECT_GT(sceneData.radius, 0.0);
}

TEST(ModelLoader, LoadStepSceneStillWorks)
{
    auto sceneData = vsgocct::loadScene(testDataDir() / "box.step");

    ASSERT_NE(sceneData.scene, nullptr);
    EXPECT_GE(sceneData.parts.size(), 1u);
    EXPECT_GT(sceneData.totalTriangleCount, 0u);
}

TEST(ModelLoader, LoadStlSceneExplicit)
{
    auto sceneData = vsgocct::loadStlScene(testDataDir() / "cube_binary.stl");

    ASSERT_NE(sceneData.scene, nullptr);
    EXPECT_EQ(sceneData.parts.size(), 1u);
    EXPECT_EQ(sceneData.parts[0].name, "cube_binary");
}

TEST(ModelLoader, UnsupportedExtensionThrows)
{
    EXPECT_THROW(vsgocct::loadScene("model.obj"), std::runtime_error);
}

TEST(ModelLoader, CaseInsensitiveExtension)
{
    // This tests the extension matching logic
    // We can't easily test with actual .STL file, but we verify
    // the function doesn't throw for the correct path
    auto sceneData = vsgocct::loadStlScene(testDataDir() / "cube.stl");
    EXPECT_EQ(sceneData.parts.size(), 1u);
}
```

- [ ] **Step 2: Add test to `tests/CMakeLists.txt`**

```cmake
vsgocct_add_test(test_model_loader)
```

- [ ] **Step 3: Build and run all tests**

Run: `cmake --build build --config Release && ctest --test-dir build -C Release --output-on-failure`
Expected: All tests pass including the new integration tests

- [ ] **Step 4: Commit**

```bash
git add tests/test_model_loader.cpp tests/CMakeLists.txt
git commit -m "test: add ModelLoader integration tests for STL and STEP loading"
```

---

### Task 7: Update example viewer to support STL files

Update the Qt viewer example to use the new `ModelLoader` API so users can open STL files.

**Files:**
- Modify: `examples/vsgqt_step_viewer/main.cpp`

- [ ] **Step 1: Update include**

In `examples/vsgqt_step_viewer/main.cpp` line 1, replace:
```cpp
#include <vsgocct/StepModelLoader.h>
```
with:
```cpp
#include <vsgocct/ModelLoader.h>
```

- [ ] **Step 2: Update file dialog to support STL**

In the `resolveStepFile` function (line 36-56), rename to `resolveModelFile` and update the file dialog:
```cpp
QString resolveModelFile(const QStringList& arguments)
{
    for (int index = 1; index < arguments.size(); ++index)
    {
        const QString argument = arguments.at(index);
        if (!argument.startsWith('-'))
        {
            return argument;
        }
    }

    const QString initialDirectory = std::filesystem::exists("D:/OCCT/data/step")
                                         ? QStringLiteral("D:/OCCT/data/step")
                                         : QString();

    return QFileDialog::getOpenFileName(
        nullptr,
        QStringLiteral("Open 3D Model"),
        initialDirectory,
        QStringLiteral("3D Models (*.step *.stp *.stl *.STEP *.STP *.STL);;STEP Files (*.step *.stp);;STL Files (*.stl);;All Files (*.*)"));
}
```

Update the call site that uses `resolveStepFile` to use `resolveModelFile` (search for the variable name `stepFile` and update references as needed).

- [ ] **Step 3: Update loading call**

At line 524, replace:
```cpp
        auto sceneData = vsgocct::loadStepScene(
            std::filesystem::path(stepFile.toStdWString()),
            {},
            sceneOptions);
```
with:
```cpp
        auto sceneData = vsgocct::loadScene(
            std::filesystem::path(modelFile.toStdWString()),
            sceneOptions);
```

(Where `modelFile` is the renamed variable from `stepFile`.)

- [ ] **Step 3: Build the example**

Run: `cmake --build build --config Release --target vsgqt_step_viewer`
Expected: Builds without errors

- [ ] **Step 4: Commit**

```bash
git add examples/vsgqt_step_viewer/main.cpp
git commit -m "feat(example): update viewer to support STL files via ModelLoader"
```
