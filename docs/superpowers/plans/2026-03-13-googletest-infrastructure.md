# GoogleTest Infrastructure Implementation Plan

> **For agentic workers:** REQUIRED: Use superpowers:subagent-driven-development (if subagents available) or superpowers:executing-plans to implement this plan. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add GoogleTest unit testing framework covering all three vsgocct modules (cad, mesh, scene).

**Architecture:** Three independent test executables (one per module) using GoogleTest via CMake FetchContent. Small STEP files committed to `tests/data/` as test fixtures. Tests use only the vsgocct public API.

**Tech Stack:** GoogleTest, CMake FetchContent, CTest, OCCT (for test data generation)

**Spec:** `docs/superpowers/specs/2026-03-13-googletest-infrastructure-design.md`

---

## File Map

| Action | File | Responsibility |
|--------|------|---------------|
| Modify | `CMakeLists.txt:75-81` | Add `BUILD_TESTING` option and `add_subdirectory(tests)` |
| Create | `tests/CMakeLists.txt` | FetchContent GoogleTest, define 3 test targets |
| Create | `tests/test_helpers.h` | `TEST_DATA_DIR` macro, path helper function |
| Create | `tests/generate_test_data.cpp` | One-shot utility to generate box.step and assembly.step |
| Create | `tests/data/box.step` | Generated: 10x20x30 box |
| Create | `tests/data/assembly.step` | Generated: box + cylinder assembly |
| Create | `tests/test_step_reader.cpp` | 5 test cases for cad module |
| Create | `tests/test_shape_mesher.cpp` | 7 test cases for mesh module |
| Create | `tests/test_scene_builder.cpp` | 5 test cases for scene module |

---

## Chunk 1: CMake Infrastructure and Test Data

### Task 1: Add BUILD_TESTING to root CMakeLists.txt

**Files:**
- Modify: `CMakeLists.txt:75-81`

- [ ] **Step 1: Add BUILD_TESTING option and tests subdirectory**

After line 80 (end of examples block) in `CMakeLists.txt`, add:

```cmake
option(BUILD_TESTING "Build unit tests" ON)
if(BUILD_TESTING)
    enable_testing()
    add_subdirectory(tests)
endif()
```

- [ ] **Step 2: Commit**

```bash
git add CMakeLists.txt
git commit -m "build: add BUILD_TESTING option and tests subdirectory"
```

---

### Task 2: Create tests/CMakeLists.txt

**Files:**
- Create: `tests/CMakeLists.txt`

- [ ] **Step 1: Write tests/CMakeLists.txt**

```cmake
include(FetchContent)
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        v1.15.2
)
# Prevent GoogleTest from overriding parent project's compiler/linker settings on Windows
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

include(GoogleTest)

# Helper function to reduce boilerplate
function(vsgocct_add_test name)
    add_executable(${name} ${name}.cpp)
    target_link_libraries(${name} PRIVATE GTest::gtest_main vsgocct)
    target_compile_definitions(${name} PRIVATE
        TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data")
    gtest_discover_tests(${name})
endfunction()

vsgocct_add_test(test_step_reader)
vsgocct_add_test(test_shape_mesher)
vsgocct_add_test(test_scene_builder)

# --- generate_test_data (utility, not a test) ---
# Links OCCT directly because it calls BRepPrimAPI and STEPControl_Writer.
# vsgocct already provides OCCT include dirs as PUBLIC.
# If TKDESTEP doesn't provide STEPControl_Writer, try adding TKXDESTEP or TKSTEP.
add_executable(generate_test_data generate_test_data.cpp)
target_link_libraries(generate_test_data PRIVATE vsgocct TKDESTEP TKPrim TKTopAlgo)
target_compile_definitions(generate_test_data PRIVATE
    TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data")
```

- [ ] **Step 2: Commit**

```bash
git add tests/CMakeLists.txt
git commit -m "build: add tests/CMakeLists.txt with GoogleTest FetchContent"
```

---

### Task 3: Create test_helpers.h

**Files:**
- Create: `tests/test_helpers.h`

- [ ] **Step 1: Write test_helpers.h**

```cpp
#pragma once

#include <filesystem>
#include <string>

// TEST_DATA_DIR is injected by CMake via target_compile_definitions
#ifndef TEST_DATA_DIR
#error "TEST_DATA_DIR must be defined by CMake"
#endif

namespace vsgocct::test
{
inline std::filesystem::path testDataPath(const std::string& filename)
{
    return std::filesystem::path(TEST_DATA_DIR) / filename;
}
} // namespace vsgocct::test
```

- [ ] **Step 2: Commit**

```bash
git add tests/test_helpers.h
git commit -m "test: add test_helpers.h with TEST_DATA_DIR path utility"
```

---

### Task 4: Create generate_test_data utility and generate STEP files

**Files:**
- Create: `tests/generate_test_data.cpp`
- Create: `tests/data/box.step` (generated output)
- Create: `tests/data/assembly.step` (generated output)

- [ ] **Step 1: Write generate_test_data.cpp**

```cpp
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRep_Builder.hxx>
#include <STEPControl_Writer.hxx>
#include <TopoDS_Compound.hxx>
#include <gp_Ax2.hxx>

#include <filesystem>
#include <iostream>
#include <string>

static void writeStep(const TopoDS_Shape& shape, const std::filesystem::path& path)
{
    STEPControl_Writer writer;
    writer.Transfer(shape, STEPControl_AsIs);
    if (writer.Write(path.string().c_str()) != IFSelect_RetDone)
    {
        std::cerr << "Failed to write: " << path << std::endl;
        std::exit(1);
    }
    std::cout << "Written: " << path << std::endl;
}

int main()
{
    std::filesystem::path dataDir(TEST_DATA_DIR);
    std::filesystem::create_directories(dataDir);

    // box.step: 10x20x30 box
    {
        BRepPrimAPI_MakeBox makeBox(10.0, 20.0, 30.0);
        writeStep(makeBox.Shape(), dataDir / "box.step");
    }

    // assembly.step: compound of box + cylinder
    {
        BRepPrimAPI_MakeBox makeBox(10.0, 20.0, 30.0);
        BRepPrimAPI_MakeCylinder makeCyl(gp_Ax2(gp_Pnt(30.0, 0.0, 0.0), gp_Dir(0.0, 0.0, 1.0)), 5.0, 15.0);

        BRep_Builder builder;
        TopoDS_Compound compound;
        builder.MakeCompound(compound);
        builder.Add(compound, makeBox.Shape());
        builder.Add(compound, makeCyl.Shape());

        writeStep(compound, dataDir / "assembly.step");
    }

    return 0;
}
```

- [ ] **Step 2: Build and run the generator**

```bash
cmake --build build --config Debug --target generate_test_data
./build/Debug/generate_test_data.exe
```

Expected output:
```
Written: .../tests/data/box.step
Written: .../tests/data/assembly.step
```

- [ ] **Step 3: Verify files exist and are small**

```bash
ls -la tests/data/
```

Expected: `box.step` and `assembly.step` exist, each <10KB.

- [ ] **Step 4: Commit**

```bash
git add tests/generate_test_data.cpp tests/data/box.step tests/data/assembly.step
git commit -m "test: add STEP test data generator and generated fixtures"
```

---

## Chunk 2: Test Files

### Task 5: Write test_step_reader.cpp

**Files:**
- Create: `tests/test_step_reader.cpp`
- Reference: `include/vsgocct/cad/StepReader.h`, `src/vsgocct/cad/StepReader.cpp`

- [ ] **Step 1: Write test_step_reader.cpp**

```cpp
#include <gtest/gtest.h>

#include "test_helpers.h"

#include <vsgocct/cad/StepReader.h>

// TopAbs_ShapeEnum and TopoDS_Shape methods (IsNull, ShapeType, NbChildren)
// are all inline/header-only, available transitively via StepReader.h -> TopoDS_Shape.hxx.
// No explicit OCCT includes needed — avoids linking against PRIVATE OCCT libraries.

using namespace vsgocct::cad;
using namespace vsgocct::test;

TEST(StepReader, ReadValidBox)
{
    auto data = readStep(testDataPath("box.step"));
    EXPECT_FALSE(data.shape.IsNull());
    EXPECT_EQ(data.shape.ShapeType(), TopAbs_SOLID);
}

TEST(StepReader, ReadAssembly)
{
    auto data = readStep(testDataPath("assembly.step"));
    EXPECT_FALSE(data.shape.IsNull());
    EXPECT_EQ(data.shape.ShapeType(), TopAbs_COMPOUND);

    // NbChildren() is inline in TopoDS_Shape.hxx — no OCCT library linkage needed
    EXPECT_GE(data.shape.NbChildren(), 2);
}

TEST(StepReader, ReadNonExistentFile)
{
    EXPECT_THROW(readStep(testDataPath("does_not_exist.step")), std::runtime_error);
}

TEST(StepReader, ReadInvalidFile)
{
    // test_helpers.h itself is not a STEP file
    EXPECT_THROW(readStep(testDataPath("../test_helpers.h")), std::runtime_error);
}

TEST(StepReader, ReadEmptyPath)
{
    EXPECT_THROW(readStep(""), std::runtime_error);
}
```

- [ ] **Step 2: Build and run tests**

```bash
cmake --build build --config Debug --target test_step_reader
ctest --test-dir build -R StepReader --output-on-failure
```

Expected: All 5 tests PASS.

- [ ] **Step 3: Commit**

```bash
git add tests/test_step_reader.cpp
git commit -m "test: add cad::readStep unit tests (5 cases)"
```

---

### Task 6: Write test_shape_mesher.cpp

**Files:**
- Create: `tests/test_shape_mesher.cpp`
- Reference: `include/vsgocct/mesh/ShapeMesher.h`, `src/vsgocct/mesh/ShapeMesher.cpp`

- [ ] **Step 1: Write test_shape_mesher.cpp**

```cpp
#include <gtest/gtest.h>

#include "test_helpers.h"

#include <vsgocct/cad/StepReader.h>
#include <vsgocct/mesh/ShapeMesher.h>

#include <cmath>

using namespace vsgocct::cad;
using namespace vsgocct::mesh;
using namespace vsgocct::test;

class ShapeMesherTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        boxShape = readStep(testDataPath("box.step")).shape;
    }
    TopoDS_Shape boxShape;
};

TEST_F(ShapeMesherTest, TriangulateBox)
{
    auto result = triangulate(boxShape);
    EXPECT_GT(result.pointCount, 0u);
    EXPECT_GE(result.triangleCount, 12u); // box has 6 faces, at least 2 triangles each
}

TEST_F(ShapeMesherTest, TriangulateBoxHasNormals)
{
    auto result = triangulate(boxShape);
    EXPECT_EQ(result.faceNormals.size(), result.facePositions.size());
}

TEST_F(ShapeMesherTest, TriangulateBoxBounds)
{
    auto result = triangulate(boxShape);
    auto size = result.boundsMax - result.boundsMin;
    // box.step is 10x20x30, allow small tolerance
    EXPECT_NEAR(size.x, 10.0, 0.1);
    EXPECT_NEAR(size.y, 20.0, 0.1);
    EXPECT_NEAR(size.z, 30.0, 0.1);
}

TEST_F(ShapeMesherTest, TriangulateBoxEdges)
{
    auto result = triangulate(boxShape);
    EXPECT_GT(result.lineSegmentCount, 0u);
}

TEST_F(ShapeMesherTest, TriangulateAssembly)
{
    auto assemblyShape = readStep(testDataPath("assembly.step")).shape;
    auto result = triangulate(assemblyShape);
    // Assembly has more geometry than a single box
    auto boxResult = triangulate(boxShape);
    EXPECT_GT(result.triangleCount, boxResult.triangleCount);
}

TEST_F(ShapeMesherTest, CustomMeshOptions)
{
    MeshOptions coarse;
    coarse.linearDeflection = 2.0;
    auto coarseResult = triangulate(boxShape, coarse);

    MeshOptions fine;
    fine.linearDeflection = 0.1;
    auto fineResult = triangulate(boxShape, fine);

    // Finer deflection should produce >= as many triangles
    EXPECT_GE(fineResult.triangleCount, coarseResult.triangleCount);
}

TEST_F(ShapeMesherTest, EmptyShape)
{
    TopoDS_Shape empty;
    EXPECT_THROW(triangulate(empty), std::runtime_error);
}
```

- [ ] **Step 2: Build and run tests**

```bash
cmake --build build --config Debug --target test_shape_mesher
ctest --test-dir build -R ShapeMesher --output-on-failure
```

Expected: All 7 tests PASS.

- [ ] **Step 3: Commit**

```bash
git add tests/test_shape_mesher.cpp
git commit -m "test: add mesh::triangulate unit tests (7 cases)"
```

---

### Task 7: Write test_scene_builder.cpp

**Files:**
- Create: `tests/test_scene_builder.cpp`
- Reference: `include/vsgocct/scene/SceneBuilder.h`, `include/vsgocct/StepSceneData.h`

- [ ] **Step 1: Write test_scene_builder.cpp**

```cpp
#include <gtest/gtest.h>

#include "test_helpers.h"

#include <vsgocct/cad/StepReader.h>
#include <vsgocct/mesh/ShapeMesher.h>
#include <vsgocct/scene/SceneBuilder.h>

#include <cmath>

using namespace vsgocct::cad;
using namespace vsgocct::mesh;
using namespace vsgocct::scene;
using namespace vsgocct::test;

class SceneBuilderTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        auto shape = readStep(testDataPath("box.step")).shape;
        meshResult = triangulate(shape);
    }
    MeshResult meshResult;
};

TEST_F(SceneBuilderTest, BuildSceneFromBox)
{
    auto sceneData = buildScene(meshResult);
    EXPECT_TRUE(sceneData.scene);
}

TEST_F(SceneBuilderTest, SwitchNodesExist)
{
    auto sceneData = buildScene(meshResult);
    EXPECT_TRUE(sceneData.pointSwitch);
    EXPECT_TRUE(sceneData.lineSwitch);
    EXPECT_TRUE(sceneData.faceSwitch);
}

TEST_F(SceneBuilderTest, ToggleVisibility)
{
    auto sceneData = buildScene(meshResult);

    // Default: all visible
    EXPECT_TRUE(sceneData.pointsVisible());
    EXPECT_TRUE(sceneData.linesVisible());
    EXPECT_TRUE(sceneData.facesVisible());

    // Toggle off
    sceneData.setPointsVisible(false);
    EXPECT_FALSE(sceneData.pointsVisible());

    sceneData.setLinesVisible(false);
    EXPECT_FALSE(sceneData.linesVisible());

    sceneData.setFacesVisible(false);
    EXPECT_FALSE(sceneData.facesVisible());

    // Toggle back on
    sceneData.setPointsVisible(true);
    EXPECT_TRUE(sceneData.pointsVisible());
}

TEST_F(SceneBuilderTest, SceneCenterAndRadius)
{
    auto sceneData = buildScene(meshResult);
    EXPECT_FALSE(std::isnan(sceneData.center.x));
    EXPECT_FALSE(std::isnan(sceneData.center.y));
    EXPECT_FALSE(std::isnan(sceneData.center.z));
    EXPECT_GT(sceneData.radius, 0.0);
}

TEST_F(SceneBuilderTest, EmptyMeshResult)
{
    MeshResult empty;
    // buildScene should handle empty input without crashing
    auto sceneData = buildScene(empty);
    EXPECT_TRUE(sceneData.scene);
}
```

- [ ] **Step 2: Build and run tests**

```bash
cmake --build build --config Debug --target test_scene_builder
ctest --test-dir build -R SceneBuilder --output-on-failure
```

Expected: All 5 tests PASS.

- [ ] **Step 3: Commit**

```bash
git add tests/test_scene_builder.cpp
git commit -m "test: add scene::buildScene unit tests (5 cases)"
```

---

## Chunk 3: Final Verification

### Task 8: Full build and test run

- [ ] **Step 1: Clean build with all tests**

```bash
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build --config Debug
```

Expected: All targets compile without errors.

- [ ] **Step 2: Run all tests**

```bash
ctest --test-dir build --output-on-failure
```

Expected: 17 tests total (5 + 7 + 5), all PASS.

- [ ] **Step 3: Verify tests can be run individually**

```bash
ctest --test-dir build -R StepReader --output-on-failure
ctest --test-dir build -R ShapeMesher --output-on-failure
ctest --test-dir build -R SceneBuilder --output-on-failure
```

Expected: Each group runs independently.
