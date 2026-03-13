# M1a: Assembly Tree Preservation — Implementation Plan

> **For agentic workers:** REQUIRED: Use superpowers:subagent-driven-development (if subagents available) or superpowers:executing-plans to implement this plan. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace flat STEP reading with XCAF-based assembly tree extraction, producing per-part VSG subgraphs with individual visibility control and per-part colors.

**Architecture:** Three-layer pipeline: `cad::readStep()` produces a `ShapeNode` tree via `STEPCAFControl_Reader`; `scene::buildAssemblyScene()` recursively walks the tree to create per-part VSG nodes wrapped in `vsg::Switch`; `StepModelLoader` facade wires them together.

**Tech Stack:** C++17, OpenCASCADE XCAF (STEPCAFControl_Reader, XCAFDoc_ShapeTool, XCAFDoc_ColorTool), VulkanSceneGraph (VSG), GoogleTest, CMake, Qt5/Qt6.

**Spec:** `docs/superpowers/specs/2026-03-13-m1a-assembly-tree-design.md`

---

## File Structure

| File | Responsibility | Action |
|------|---------------|--------|
| `include/vsgocct/cad/StepReader.h` | ShapeNode/AssemblyData types + readStep API | Rewrite |
| `src/vsgocct/cad/StepReader.cpp` | XCAF reader + recursive tree builder | Rewrite |
| `include/vsgocct/scene/SceneBuilder.h` | AssemblySceneData/PartSceneNode types + buildAssemblyScene API | Rewrite |
| `src/vsgocct/scene/SceneBuilder.cpp` | Recursive scene graph builder with per-part color | Rewrite |
| `include/vsgocct/StepSceneData.h` | (obsolete) | Delete |
| `include/vsgocct/StepModelLoader.h` | Facade header | Update return type |
| `src/vsgocct/StepModelLoader.cpp` | Facade impl | Update pipeline |
| `src/vsgocct/CMakeLists.txt` | Library build config | Add XCAF link targets, remove StepSceneData |
| `tests/generate_test_data.cpp` | Test STEP file generator | Add colored_box + nested_assembly |
| `tests/test_step_reader.cpp` | cad module unit tests | Rewrite for ShapeNode tree |
| `tests/test_scene_builder.cpp` | scene module unit tests | Rewrite for AssemblySceneData |
| `tests/CMakeLists.txt` | Test build config | Add XCAF link targets for generate_test_data |
| `examples/vsgqt_step_viewer/main.cpp` | Qt viewer example | Update to AssemblySceneData + part list UI |

---

## Chunk 1: cad Module — Data Types and XCAF Reader

### Task 1: Update CMakeLists.txt with XCAF dependencies

**Files:**
- Modify: `src/vsgocct/CMakeLists.txt`

- [ ] **Step 1: Add XCAF link targets and remove StepSceneData header**

In `src/vsgocct/CMakeLists.txt`, make two changes:

1. In `VSGOCCT_HEADERS`, remove the `StepSceneData.h` line.
2. In `target_link_libraries`, add XCAF libraries to the PRIVATE section.

```cmake
set(VSGOCCT_HEADERS
    ../../include/vsgocct/StepModelLoader.h
    ../../include/vsgocct/cad/StepReader.h
    ../../include/vsgocct/mesh/ShapeMesher.h
    ../../include/vsgocct/scene/SceneBuilder.h
)

target_link_libraries(vsgocct
    PUBLIC
        vsg::vsg
    PRIVATE
        TKXDESTEP
        TKLCAF
        TKXCAF
        TKMesh
        TKTopAlgo
)
```

Note: `TKDESTEP` is removed because `TKXDESTEP` depends on it internally. `TKLCAF` provides `TDocStd_Document`; `TKXCAF` provides `XCAFDoc_ShapeTool` and `XCAFDoc_ColorTool`.

- [ ] **Step 2: Verify build still compiles**

Run: `cmake --build build --config Debug 2>&1 | head -30`
Expected: Build succeeds (existing code still references old headers, but nothing new links yet — just library targets added).

Note: The build will start failing after Task 2 when we change headers. That's expected — we'll fix it in subsequent tasks.

- [ ] **Step 3: Commit**

```bash
git add src/vsgocct/CMakeLists.txt
git commit -m "build: add XCAF link targets for assembly tree support"
```

---

### Task 2: Rewrite StepReader.h with ShapeNode and AssemblyData

**Files:**
- Rewrite: `include/vsgocct/cad/StepReader.h`

- [ ] **Step 1: Write the new header**

Replace the entire content of `include/vsgocct/cad/StepReader.h`:

```cpp
#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include <TopLoc_Location.hxx>
#include <TopoDS_Shape.hxx>

namespace vsgocct::cad
{
struct ReaderOptions
{
};

enum class ShapeNodeType
{
    Assembly, // Has children, no geometry
    Part      // Leaf node, holds actual TopoDS_Shape
};

struct ShapeNodeColor
{
    float r = 0.74f;
    float g = 0.79f;
    float b = 0.86f;
    bool isSet = false;
};

struct ShapeNode
{
    ShapeNodeType type = ShapeNodeType::Part;
    std::string name;
    ShapeNodeColor color;
    TopoDS_Shape shape;
    TopLoc_Location location;
    std::vector<ShapeNode> children;
};

struct AssemblyData
{
    std::vector<ShapeNode> roots;
};

AssemblyData readStep(const std::filesystem::path& stepFile,
                      const ReaderOptions& options = {});
} // namespace vsgocct::cad
```

- [ ] **Step 2: Commit (build will break — that's expected until Task 3 + 4 complete)**

```bash
git add include/vsgocct/cad/StepReader.h
git commit -m "feat(cad): define ShapeNode tree and AssemblyData types"
```

---

### Task 3: Rewrite StepReader.cpp with XCAF implementation

**Files:**
- Rewrite: `src/vsgocct/cad/StepReader.cpp`

- [ ] **Step 1: Write the XCAF-based implementation**

Replace the entire content of `src/vsgocct/cad/StepReader.cpp`:

```cpp
#include <vsgocct/cad/StepReader.h>

#include <STEPCAFControl_Reader.hxx>
#include <TDF_LabelSequence.hxx>
#include <TDataStd_Name.hxx>
#include <TDocStd_Document.hxx>
#include <XCAFDoc_ColorTool.hxx>
#include <XCAFDoc_DocumentTool.hxx>
#include <XCAFDoc_ShapeTool.hxx>

#include <Quantity_Color.hxx>

#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace vsgocct::cad
{
namespace
{
std::string readLabelName(const TDF_Label& label)
{
    Handle(TDataStd_Name) nameAttr;
    if (label.FindAttribute(TDataStd_Name::GetID(), nameAttr))
    {
        return std::string(nameAttr->Get().ToExtString(),
                           nameAttr->Get().ToExtString() + nameAttr->Get().Length());
    }
    return {};
}

ShapeNodeColor readLabelColor(const TDF_Label& label,
                              const Handle(XCAFDoc_ColorTool)& colorTool)
{
    Quantity_Color qc;
    if (colorTool->GetColor(label, XCAFDoc_ColorSurf, qc))
    {
        return {static_cast<float>(qc.Red()),
                static_cast<float>(qc.Green()),
                static_cast<float>(qc.Blue()),
                true};
    }
    return {};
}

ShapeNode buildShapeNode(const TDF_Label& label,
                         const Handle(XCAFDoc_ShapeTool)& shapeTool,
                         const Handle(XCAFDoc_ColorTool)& colorTool,
                         const ShapeNodeColor& parentColor)
{
    ShapeNode node;
    node.name = readLabelName(label);
    node.color = readLabelColor(label, colorTool);

    // Color inheritance: if not set on this label, inherit from parent
    if (!node.color.isSet && parentColor.isSet)
    {
        node.color = parentColor;
    }

    // Resolve references: extract location and follow to prototype
    TDF_Label resolvedLabel = label;
    if (shapeTool->IsReference(label))
    {
        // Extract transform from the component label
        node.location = shapeTool->GetLocation(label);

        TDF_Label refLabel;
        shapeTool->GetReferredShape(label, refLabel);
        resolvedLabel = refLabel;

        // Re-read name/color from prototype if not set on component
        if (node.name.empty())
        {
            node.name = readLabelName(resolvedLabel);
        }
        if (!node.color.isSet)
        {
            node.color = readLabelColor(resolvedLabel, colorTool);
            if (!node.color.isSet && parentColor.isSet)
            {
                node.color = parentColor;
            }
        }
    }

    // Process the resolved label (prototype)
    if (shapeTool->IsAssembly(resolvedLabel))
    {
        node.type = ShapeNodeType::Assembly;
        TDF_LabelSequence components;
        shapeTool->GetComponents(resolvedLabel, components);
        for (Standard_Integer i = 1; i <= components.Length(); ++i)
        {
            try
            {
                node.children.push_back(
                    buildShapeNode(components.Value(i), shapeTool, colorTool, node.color));
            }
            catch (const std::exception& ex)
            {
                std::cerr << "Warning: skipping component " << i
                          << ": " << ex.what() << std::endl;
            }
        }
    }
    else
    {
        // Simple shape (leaf Part)
        node.type = ShapeNodeType::Part;
        node.shape = shapeTool->GetShape(resolvedLabel);
        if (node.shape.IsNull())
        {
            throw std::runtime_error("Null shape at label: " + node.name);
        }
    }

    return node;
}

std::size_t countParts(const ShapeNode& node)
{
    if (node.type == ShapeNodeType::Part)
    {
        return 1;
    }
    std::size_t count = 0;
    for (const auto& child : node.children)
    {
        count += countParts(child);
    }
    return count;
}
} // namespace

AssemblyData readStep(const std::filesystem::path& stepFile, const ReaderOptions& /*options*/)
{
    std::ifstream input(stepFile, std::ios::binary);
    if (!input)
    {
        throw std::runtime_error("Failed to open STEP file: " + stepFile.u8string());
    }

    STEPCAFControl_Reader reader;
    reader.SetNameMode(true);
    reader.SetColorMode(true);

    const std::string displayName = stepFile.filename().u8string();
    const auto status = reader.ReadStream(displayName.c_str(), input);
    if (status != IFSelect_RetDone)
    {
        throw std::runtime_error("OCCT failed to read STEP data from: " + stepFile.u8string());
    }

    Handle(TDocStd_Document) doc = new TDocStd_Document("XDE");
    if (!reader.Transfer(doc))
    {
        throw std::runtime_error("OCCT failed to transfer STEP document: " + stepFile.u8string());
    }

    auto shapeTool = XCAFDoc_DocumentTool::ShapeTool(doc->Main());
    auto colorTool = XCAFDoc_DocumentTool::ColorTool(doc->Main());

    TDF_LabelSequence freeLabels;
    shapeTool->GetFreeShapes(freeLabels);

    AssemblyData assembly;
    ShapeNodeColor noParentColor;
    for (Standard_Integer i = 1; i <= freeLabels.Length(); ++i)
    {
        try
        {
            assembly.roots.push_back(
                buildShapeNode(freeLabels.Value(i), shapeTool, colorTool, noParentColor));
        }
        catch (const std::exception& ex)
        {
            std::cerr << "Warning: skipping free shape " << i
                      << ": " << ex.what() << std::endl;
        }
    }

    // Verify at least one valid part exists
    std::size_t totalParts = 0;
    for (const auto& root : assembly.roots)
    {
        totalParts += countParts(root);
    }
    if (totalParts == 0)
    {
        throw std::runtime_error("No valid parts found in STEP file: " + stepFile.u8string());
    }

    return assembly;
}
} // namespace vsgocct::cad
```

- [ ] **Step 2: Commit**

```bash
git add src/vsgocct/cad/StepReader.cpp
git commit -m "feat(cad): implement XCAF-based STEP reader with assembly tree"
```

---

### Task 4: Update test data generator with XCAF test files

**Files:**
- Modify: `tests/generate_test_data.cpp`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Add XCAF test data generation**

Replace the entire content of `tests/generate_test_data.cpp`:

```cpp
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRep_Builder.hxx>
#include <STEPControl_Writer.hxx>
#include <TopoDS_Compound.hxx>
#include <gp_Ax2.hxx>

// XCAF includes for colored/named assembly test data
#include <STEPCAFControl_Writer.hxx>
#include <TDF_Label.hxx>
#include <TDataStd_Name.hxx>
#include <TDocStd_Document.hxx>
#include <TopLoc_Location.hxx>
#include <XCAFDoc_ColorTool.hxx>
#include <XCAFDoc_DocumentTool.hxx>
#include <XCAFDoc_ShapeTool.hxx>

#include <Quantity_Color.hxx>
#include <gp_Trsf.hxx>

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

static void writeXdeStep(const Handle(TDocStd_Document)& doc, const std::filesystem::path& path)
{
    STEPCAFControl_Writer writer;
    writer.SetNameMode(true);
    writer.SetColorMode(true);
    if (!writer.Transfer(doc, STEPControl_AsIs))
    {
        std::cerr << "Failed to transfer XDE document for: " << path << std::endl;
        std::exit(1);
    }
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

    // box.step: 10x20x30 box (plain STEP, no XCAF metadata)
    {
        BRepPrimAPI_MakeBox makeBox(10.0, 20.0, 30.0);
        writeStep(makeBox.Shape(), dataDir / "box.step");
    }

    // assembly.step: compound of box + cylinder (plain STEP)
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

    // colored_box.step: single box with red surface color (XCAF)
    {
        Handle(TDocStd_Document) doc = new TDocStd_Document("XDE");
        auto shapeTool = XCAFDoc_DocumentTool::ShapeTool(doc->Main());
        auto colorTool = XCAFDoc_DocumentTool::ColorTool(doc->Main());

        BRepPrimAPI_MakeBox makeBox(10.0, 20.0, 30.0);
        TDF_Label boxLabel = shapeTool->AddShape(makeBox.Shape());
        TDataStd_Name::Set(boxLabel, "RedBox");
        colorTool->SetColor(boxLabel, Quantity_Color(1.0, 0.0, 0.0, Quantity_TOC_RGB), XCAFDoc_ColorSurf);

        writeXdeStep(doc, dataDir / "colored_box.step");
    }

    // nested_assembly.step: Assembly -> SubAssembly -> (Box + Cylinder)
    // with distinct names, colors, and non-identity locations
    {
        Handle(TDocStd_Document) doc = new TDocStd_Document("XDE");
        auto shapeTool = XCAFDoc_DocumentTool::ShapeTool(doc->Main());
        auto colorTool = XCAFDoc_DocumentTool::ColorTool(doc->Main());

        // Create prototype shapes
        BRepPrimAPI_MakeBox makeBox(10.0, 20.0, 30.0);
        TDF_Label boxProto = shapeTool->AddShape(makeBox.Shape());
        TDataStd_Name::Set(boxProto, "Box");
        colorTool->SetColor(boxProto, Quantity_Color(0.0, 0.5, 1.0, Quantity_TOC_RGB), XCAFDoc_ColorSurf);

        BRepPrimAPI_MakeCylinder makeCyl(5.0, 15.0);
        TDF_Label cylProto = shapeTool->AddShape(makeCyl.Shape());
        TDataStd_Name::Set(cylProto, "Cylinder");
        colorTool->SetColor(cylProto, Quantity_Color(1.0, 0.5, 0.0, Quantity_TOC_RGB), XCAFDoc_ColorSurf);

        // Create SubAssembly containing Box and Cylinder
        TDF_Label subAssembly = shapeTool->NewShape();
        TDataStd_Name::Set(subAssembly, "SubAssembly");
        shapeTool->MakeAssembly(subAssembly);

        // Add Box at origin
        TDF_Label boxComp = shapeTool->AddComponent(subAssembly, boxProto, TopLoc_Location());

        // Add Cylinder translated to (30, 0, 0)
        gp_Trsf cylTransform;
        cylTransform.SetTranslation(gp_Vec(30.0, 0.0, 0.0));
        TDF_Label cylComp = shapeTool->AddComponent(subAssembly, cylProto, TopLoc_Location(cylTransform));

        // Create root Assembly containing SubAssembly
        TDF_Label rootAssembly = shapeTool->NewShape();
        TDataStd_Name::Set(rootAssembly, "RootAssembly");
        shapeTool->MakeAssembly(rootAssembly);

        gp_Trsf subTransform;
        subTransform.SetTranslation(gp_Vec(0.0, 50.0, 0.0));
        TDF_Label subComp = shapeTool->AddComponent(rootAssembly, subAssembly, TopLoc_Location(subTransform));

        // Suppress unused variable warnings
        (void)boxComp;
        (void)cylComp;
        (void)subComp;

        writeXdeStep(doc, dataDir / "nested_assembly.step");
    }

    // shared_instances.step: same box prototype used twice with different locations
    {
        Handle(TDocStd_Document) doc = new TDocStd_Document("XDE");
        auto shapeTool = XCAFDoc_DocumentTool::ShapeTool(doc->Main());
        auto colorTool = XCAFDoc_DocumentTool::ColorTool(doc->Main());

        BRepPrimAPI_MakeBox makeBox(10.0, 10.0, 10.0);
        TDF_Label boxProto = shapeTool->AddShape(makeBox.Shape());
        TDataStd_Name::Set(boxProto, "SharedBox");
        colorTool->SetColor(boxProto, Quantity_Color(0.0, 1.0, 0.0, Quantity_TOC_RGB), XCAFDoc_ColorSurf);

        TDF_Label rootAssembly = shapeTool->NewShape();
        TDataStd_Name::Set(rootAssembly, "SharedAssembly");
        shapeTool->MakeAssembly(rootAssembly);

        // Instance 1: at origin
        shapeTool->AddComponent(rootAssembly, boxProto, TopLoc_Location());

        // Instance 2: translated to (25, 0, 0)
        gp_Trsf t2;
        t2.SetTranslation(gp_Vec(25.0, 0.0, 0.0));
        shapeTool->AddComponent(rootAssembly, boxProto, TopLoc_Location(t2));

        writeXdeStep(doc, dataDir / "shared_instances.step");
    }

    return 0;
}
```

- [ ] **Step 2: Update tests/CMakeLists.txt for generate_test_data XCAF dependencies**

In `tests/CMakeLists.txt`, update the `generate_test_data` link libraries:

```cmake
add_executable(generate_test_data generate_test_data.cpp)
target_link_libraries(generate_test_data PRIVATE vsgocct TKXDESTEP TKLCAF TKXCAF TKPrim TKTopAlgo)
target_compile_definitions(generate_test_data PRIVATE
    TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data")
```

Changes: add `TKXDESTEP`, `TKLCAF`, `TKXCAF` to link libraries. Remove `TKDESTEP` (covered by `TKXDESTEP`).

- [ ] **Step 3: Build and run generate_test_data**

Run: `cmake --build build --config Debug --target generate_test_data && build\tests\Debug\generate_test_data.exe`
Expected output:
```
Written: .../data/box.step
Written: .../data/assembly.step
Written: .../data/colored_box.step
Written: .../data/nested_assembly.step
Written: .../data/shared_instances.step
```

- [ ] **Step 4: Commit**

```bash
git add tests/generate_test_data.cpp tests/CMakeLists.txt
git commit -m "test: add XCAF test data generation for colored and nested assemblies"
```

---

### Task 5: Write cad module tests

**Files:**
- Rewrite: `tests/test_step_reader.cpp`

- [ ] **Step 1: Write the new test file**

Replace the entire content of `tests/test_step_reader.cpp`:

```cpp
#include <gtest/gtest.h>

#include "test_helpers.h"

#include <vsgocct/cad/StepReader.h>

using namespace vsgocct::cad;
using namespace vsgocct::test;

// --- Error handling tests (unchanged behavior) ---

TEST(StepReader, ReadNonExistentFile)
{
    EXPECT_THROW(readStep(testDataPath("does_not_exist.step")), std::runtime_error);
}

TEST(StepReader, ReadInvalidFile)
{
    EXPECT_THROW(readStep(testDataPath("../test_helpers.h")), std::runtime_error);
}

TEST(StepReader, ReadEmptyPath)
{
    EXPECT_THROW(readStep(""), std::runtime_error);
}

// --- Single part STEP (plain, non-XCAF) ---

TEST(StepReader, SinglePartBox)
{
    auto assembly = readStep(testDataPath("box.step"));
    ASSERT_FALSE(assembly.roots.empty());

    const auto& root = assembly.roots.front();
    // Plain STEP produces a single Part node (no XCAF assembly metadata)
    EXPECT_EQ(root.type, ShapeNodeType::Part);
    EXPECT_FALSE(root.shape.IsNull());
}

// --- Plain assembly (compound, non-XCAF) ---

TEST(StepReader, PlainAssemblyBackwardCompat)
{
    auto assembly = readStep(testDataPath("assembly.step"));
    ASSERT_FALSE(assembly.roots.empty());
    // Plain compound should still produce valid tree nodes
    // (may be Assembly with Part children, or multiple Part roots)
}

// --- Colored XCAF STEP ---

TEST(StepReader, ColoredBoxHasColor)
{
    auto assembly = readStep(testDataPath("colored_box.step"));
    ASSERT_FALSE(assembly.roots.empty());

    const auto& root = assembly.roots.front();
    EXPECT_EQ(root.type, ShapeNodeType::Part);
    EXPECT_FALSE(root.shape.IsNull());
    EXPECT_TRUE(root.color.isSet);
    // Red color: (1.0, 0.0, 0.0)
    EXPECT_NEAR(root.color.r, 1.0f, 0.01f);
    EXPECT_NEAR(root.color.g, 0.0f, 0.01f);
    EXPECT_NEAR(root.color.b, 0.0f, 0.01f);
}

TEST(StepReader, ColoredBoxHasName)
{
    auto assembly = readStep(testDataPath("colored_box.step"));
    ASSERT_FALSE(assembly.roots.empty());

    const auto& root = assembly.roots.front();
    EXPECT_FALSE(root.name.empty());
}

// --- Nested XCAF assembly ---

TEST(StepReader, NestedAssemblyStructure)
{
    auto assembly = readStep(testDataPath("nested_assembly.step"));
    ASSERT_FALSE(assembly.roots.empty());

    const auto& root = assembly.roots.front();
    EXPECT_EQ(root.type, ShapeNodeType::Assembly);
    EXPECT_FALSE(root.children.empty());

    // Root should contain SubAssembly which contains Box + Cylinder
    // Navigate to find at least 2 Part leaf nodes
    std::size_t partCount = 0;
    std::function<void(const ShapeNode&)> countParts = [&](const ShapeNode& n)
    {
        if (n.type == ShapeNodeType::Part)
        {
            ++partCount;
            EXPECT_FALSE(n.shape.IsNull());
        }
        for (const auto& child : n.children)
        {
            countParts(child);
        }
    };
    countParts(root);
    EXPECT_GE(partCount, 2u);
}

TEST(StepReader, NestedAssemblyColors)
{
    auto assembly = readStep(testDataPath("nested_assembly.step"));
    ASSERT_FALSE(assembly.roots.empty());

    // Collect all Part node colors
    std::vector<ShapeNodeColor> partColors;
    std::function<void(const ShapeNode&)> collectColors = [&](const ShapeNode& n)
    {
        if (n.type == ShapeNodeType::Part)
        {
            partColors.push_back(n.color);
        }
        for (const auto& child : n.children)
        {
            collectColors(child);
        }
    };
    collectColors(assembly.roots.front());

    ASSERT_GE(partColors.size(), 2u);
    // At least some parts should have color set (from prototype or inheritance)
    bool anyColorSet = false;
    for (const auto& c : partColors)
    {
        if (c.isSet) anyColorSet = true;
    }
    EXPECT_TRUE(anyColorSet);
}

TEST(StepReader, NestedAssemblyHasNonIdentityLocation)
{
    auto assembly = readStep(testDataPath("nested_assembly.step"));
    ASSERT_FALSE(assembly.roots.empty());

    const auto& root = assembly.roots.front();
    // Root assembly's children (the SubAssembly component) should have a non-identity location
    bool foundNonIdentityLocation = false;
    std::function<void(const ShapeNode&)> checkLocations = [&](const ShapeNode& n)
    {
        if (!n.location.IsIdentity())
        {
            foundNonIdentityLocation = true;
        }
        for (const auto& child : n.children)
        {
            checkLocations(child);
        }
    };
    checkLocations(root);
    EXPECT_TRUE(foundNonIdentityLocation);
}

// --- Shared instances ---

TEST(StepReader, SharedInstancesProduceSeparateNodes)
{
    auto assembly = readStep(testDataPath("shared_instances.step"));
    ASSERT_FALSE(assembly.roots.empty());

    const auto& root = assembly.roots.front();
    EXPECT_EQ(root.type, ShapeNodeType::Assembly);

    // Should have 2 Part children (same prototype, different instances)
    std::size_t partCount = 0;
    std::function<void(const ShapeNode&)> countParts = [&](const ShapeNode& n)
    {
        if (n.type == ShapeNodeType::Part) ++partCount;
        for (const auto& child : n.children) countParts(child);
    };
    countParts(root);
    EXPECT_EQ(partCount, 2u);
}
```

- [ ] **Step 2: Build and run tests**

Run: `cmake --build build --config Debug --target test_step_reader && build\tests\Debug\test_step_reader.exe`
Expected: All tests PASS. (Test data must have been regenerated first — run `generate_test_data` if needed.)

- [ ] **Step 3: Commit**

```bash
git add tests/test_step_reader.cpp
git commit -m "test(cad): rewrite step reader tests for ShapeNode assembly tree"
```

---

## Chunk 2: scene Module — AssemblySceneData and Per-Part Color

### Task 6: Rewrite SceneBuilder.h with AssemblySceneData

**Files:**
- Rewrite: `include/vsgocct/scene/SceneBuilder.h`
- Delete: `include/vsgocct/StepSceneData.h`

- [ ] **Step 1: Write the new SceneBuilder.h header**

Replace the entire content of `include/vsgocct/scene/SceneBuilder.h`:

```cpp
#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include <vsg/all.h>

#include <vsgocct/cad/StepReader.h>
#include <vsgocct/mesh/ShapeMesher.h>

namespace vsgocct::scene
{
struct SceneOptions
{
    // Reserved for future options
};

struct PartSceneNode
{
    std::string name;
    vsg::ref_ptr<vsg::Switch> switchNode;
};

struct AssemblySceneData
{
    vsg::ref_ptr<vsg::Node> scene;
    std::vector<PartSceneNode> parts;

    vsg::dvec3 center;
    double radius = 1.0;

    std::size_t totalTriangleCount = 0;
    std::size_t totalLineSegmentCount = 0;
    std::size_t totalPointCount = 0;
};

AssemblySceneData buildAssemblyScene(
    const cad::AssemblyData& assembly,
    const mesh::MeshOptions& meshOptions = {},
    const SceneOptions& sceneOptions = {});
} // namespace vsgocct::scene
```

- [ ] **Step 2: Delete StepSceneData.h**

```bash
git rm include/vsgocct/StepSceneData.h
```

- [ ] **Step 3: Commit**

```bash
git add include/vsgocct/scene/SceneBuilder.h
git commit -m "feat(scene): define AssemblySceneData and buildAssemblyScene API"
```

---

### Task 7: Rewrite SceneBuilder.cpp with recursive assembly builder

**Files:**
- Rewrite: `src/vsgocct/scene/SceneBuilder.cpp`

- [ ] **Step 1: Write the new implementation**

Replace the entire content of `src/vsgocct/scene/SceneBuilder.cpp`:

```cpp
#include <vsgocct/scene/SceneBuilder.h>

#include <vsg/all.h>

#include <algorithm>
#include <cstddef>
#include <limits>
#include <vector>

namespace vsgocct::scene
{
namespace
{
constexpr const char* FACE_VERT_SHADER = R"(
#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(push_constant) uniform PushConstants
{
    mat4 projection;
    mat4 modelView;
    vec4 baseColor;
};

layout(location = 0) in vec3 vertex;
layout(location = 1) in vec3 normal;
layout(location = 0) out vec3 viewNormal;
layout(location = 1) out vec3 partColor;

out gl_PerVertex
{
    vec4 gl_Position;
};

void main()
{
    vec4 viewVertex = modelView * vec4(vertex, 1.0);
    viewNormal = mat3(modelView) * normal;
    partColor = baseColor.rgb;
    gl_Position = projection * viewVertex;
}
)";

constexpr const char* FACE_FRAG_SHADER = R"(
#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(location = 0) in vec3 viewNormal;
layout(location = 1) in vec3 partColor;
layout(location = 0) out vec4 fragmentColor;

void main()
{
    vec3 normal = normalize(gl_FrontFacing ? viewNormal : -viewNormal);
    vec3 lightDirection = normalize(vec3(0.35, 0.55, 1.0));
    float diffuse = max(dot(normal, lightDirection), 0.0);
    vec3 shadedColor = partColor * (0.24 + 0.76 * diffuse);
    fragmentColor = vec4(shadedColor, 1.0);
}
)";

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

vsg::ref_ptr<vsg::BindGraphicsPipeline> createPrimitivePipeline(
    const char* vertexShaderSource,
    const char* fragmentShaderSource,
    VkPrimitiveTopology topology,
    bool includeNormals,
    bool depthWrite,
    uint32_t pushConstantSize = 128)
{
    vsg::DescriptorSetLayouts descriptorSetLayouts;
    vsg::PushConstantRanges pushConstantRanges{
        {VK_SHADER_STAGE_VERTEX_BIT, 0, pushConstantSize}};
    auto pipelineLayout = vsg::PipelineLayout::create(descriptorSetLayouts, pushConstantRanges);

    auto vertexShaderHints = vsg::ShaderCompileSettings::create();
    auto vertexShader = vsg::ShaderStage::create(
        VK_SHADER_STAGE_VERTEX_BIT, "main", vertexShaderSource, vertexShaderHints);
    auto fragmentShaderHints = vsg::ShaderCompileSettings::create();
    auto fragmentShader = vsg::ShaderStage::create(
        VK_SHADER_STAGE_FRAGMENT_BIT, "main", fragmentShaderSource, fragmentShaderHints);
    auto shaderStages = vsg::ShaderStages{vertexShader, fragmentShader};

    auto vertexInputState = vsg::VertexInputState::create();
    auto& bindings = vertexInputState->vertexBindingDescriptions;
    auto& attributes = vertexInputState->vertexAttributeDescriptions;

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

    auto rasterizationState = vsg::RasterizationState::create();
    rasterizationState->cullMode = VK_CULL_MODE_NONE;
    rasterizationState->lineWidth = 1.0f;

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
    if (positions.empty())
    {
        return vsg::Group::create();
    }

    auto positionArray = vsg::vec3Array::create(static_cast<uint32_t>(positions.size()));
    auto indices = vsg::uintArray::create(static_cast<uint32_t>(positions.size()));

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

vsg::ref_ptr<vsg::Node> createFaceNode(
    const std::vector<vsg::vec3>& facePositions,
    const std::vector<vsg::vec3>& faceNormals,
    const vsg::vec4& color)
{
    if (facePositions.empty())
    {
        return vsg::Group::create();
    }

    auto positions = vsg::vec3Array::create(static_cast<uint32_t>(facePositions.size()));
    auto normals = vsg::vec3Array::create(static_cast<uint32_t>(faceNormals.size()));
    auto indices = vsg::uintArray::create(static_cast<uint32_t>(facePositions.size()));

    for (std::size_t index = 0; index < facePositions.size(); ++index)
    {
        (*positions)[static_cast<uint32_t>(index)] = facePositions[index];
        (*normals)[static_cast<uint32_t>(index)] = faceNormals[index];
        (*indices)[static_cast<uint32_t>(index)] = static_cast<uint32_t>(index);
    }

    auto drawCommands = vsg::VertexIndexDraw::create();
    drawCommands->assignArrays(vsg::DataList{positions, normals});
    drawCommands->assignIndices(indices);
    drawCommands->indexCount = indices->width();
    drawCommands->instanceCount = 1;

    // Face pipeline uses 144 bytes push constant (128 for matrices + 16 for baseColor)
    auto facePipeline = createPrimitivePipeline(
        FACE_VERT_SHADER, FACE_FRAG_SHADER,
        VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
        true, true, 144);

    auto stateGroup = vsg::StateGroup::create();
    stateGroup->add(facePipeline);
    stateGroup->addChild(drawCommands);
    return stateGroup;
}

struct BoundsAccumulator
{
    vsg::dvec3 min{std::numeric_limits<double>::max(),
                   std::numeric_limits<double>::max(),
                   std::numeric_limits<double>::max()};
    vsg::dvec3 max{std::numeric_limits<double>::lowest(),
                   std::numeric_limits<double>::lowest(),
                   std::numeric_limits<double>::lowest()};
    bool valid = false;

    void expand(const vsg::dvec3& bmin, const vsg::dvec3& bmax)
    {
        min.x = std::min(min.x, bmin.x);
        min.y = std::min(min.y, bmin.y);
        min.z = std::min(min.z, bmin.z);
        max.x = std::max(max.x, bmax.x);
        max.y = std::max(max.y, bmax.y);
        max.z = std::max(max.z, bmax.z);
        valid = true;
    }
};

vsg::vec4 resolveColor(const cad::ShapeNodeColor& color)
{
    return vsg::vec4(color.r, color.g, color.b, 1.0f);
}

void buildNodeSubgraph(
    const cad::ShapeNode& shapeNode,
    const vsg::ref_ptr<vsg::Group>& parentGroup,
    const TopLoc_Location& accumulatedLocation,
    std::vector<PartSceneNode>& parts,
    BoundsAccumulator& bounds,
    const mesh::MeshOptions& meshOptions,
    std::size_t& totalTriangles,
    std::size_t& totalLines,
    std::size_t& totalPoints)
{
    TopLoc_Location currentLocation = accumulatedLocation * shapeNode.location;

    if (shapeNode.type == cad::ShapeNodeType::Assembly)
    {
        auto group = vsg::Group::create();
        for (const auto& child : shapeNode.children)
        {
            buildNodeSubgraph(child, group, currentLocation, parts, bounds,
                              meshOptions, totalTriangles, totalLines, totalPoints);
        }
        parentGroup->addChild(group);
    }
    else // Part
    {
        TopoDS_Shape locatedShape = shapeNode.shape.Located(currentLocation);
        auto meshResult = mesh::triangulate(locatedShape, meshOptions);

        auto color = resolveColor(shapeNode.color);
        auto faceNode = createFaceNode(meshResult.facePositions, meshResult.faceNormals, color);
        auto lineNode = createPositionOnlyNode(
            meshResult.linePositions,
            createPrimitivePipeline(LINE_VERT_SHADER, LINE_FRAG_SHADER,
                                   VK_PRIMITIVE_TOPOLOGY_LINE_LIST, false, false));
        auto pointNode = createPositionOnlyNode(
            meshResult.pointPositions,
            createPrimitivePipeline(POINT_VERT_SHADER, POINT_FRAG_SHADER,
                                   VK_PRIMITIVE_TOPOLOGY_POINT_LIST, false, false));

        auto partGroup = vsg::Group::create();
        partGroup->addChild(faceNode);
        partGroup->addChild(lineNode);
        partGroup->addChild(pointNode);

        auto partSwitch = vsg::Switch::create();
        partSwitch->addChild(true, partGroup);
        parentGroup->addChild(partSwitch);

        parts.push_back({shapeNode.name, partSwitch});

        totalTriangles += meshResult.triangleCount;
        totalLines += meshResult.lineSegmentCount;
        totalPoints += meshResult.pointCount;

        if (meshResult.hasGeometry())
        {
            bounds.expand(meshResult.boundsMin, meshResult.boundsMax);
        }
    }
}
} // namespace

AssemblySceneData buildAssemblyScene(
    const cad::AssemblyData& assembly,
    const mesh::MeshOptions& meshOptions,
    const SceneOptions& /*sceneOptions*/)
{
    auto root = vsg::Group::create();
    std::vector<PartSceneNode> parts;
    BoundsAccumulator bounds;
    std::size_t totalTriangles = 0;
    std::size_t totalLines = 0;
    std::size_t totalPoints = 0;

    TopLoc_Location identity;
    for (const auto& rootNode : assembly.roots)
    {
        buildNodeSubgraph(rootNode, root, identity, parts, bounds,
                          meshOptions, totalTriangles, totalLines, totalPoints);
    }

    AssemblySceneData sceneData;
    sceneData.scene = root;
    sceneData.parts = std::move(parts);

    if (bounds.valid)
    {
        sceneData.center = (bounds.min + bounds.max) * 0.5;
        sceneData.radius = vsg::length(bounds.max - bounds.min) * 0.5;
        sceneData.radius = std::max(sceneData.radius, 1.0);
    }

    sceneData.totalTriangleCount = totalTriangles;
    sceneData.totalLineSegmentCount = totalLines;
    sceneData.totalPointCount = totalPoints;

    return sceneData;
}
} // namespace vsgocct::scene
```

- [ ] **Step 2: Commit**

```bash
git add src/vsgocct/scene/SceneBuilder.cpp
git commit -m "feat(scene): implement recursive assembly scene builder with per-part color"
```

---

### Task 8: Update facade (StepModelLoader)

**Files:**
- Modify: `include/vsgocct/StepModelLoader.h`
- Modify: `src/vsgocct/StepModelLoader.cpp`

- [ ] **Step 1: Rewrite StepModelLoader.h**

Replace the entire content of `include/vsgocct/StepModelLoader.h`:

```cpp
#pragma once

#include <filesystem>

#include <vsgocct/scene/SceneBuilder.h>

namespace vsgocct
{
scene::AssemblySceneData loadStepScene(const std::filesystem::path& stepFile);
} // namespace vsgocct
```

- [ ] **Step 2: Rewrite StepModelLoader.cpp**

Replace the entire content of `src/vsgocct/StepModelLoader.cpp`:

```cpp
#include <vsgocct/StepModelLoader.h>

#include <vsgocct/cad/StepReader.h>
#include <vsgocct/scene/SceneBuilder.h>

namespace vsgocct
{
scene::AssemblySceneData loadStepScene(const std::filesystem::path& stepFile)
{
    auto assembly = cad::readStep(stepFile);
    return scene::buildAssemblyScene(assembly);
}
} // namespace vsgocct
```

- [ ] **Step 3: Build the library**

Run: `cmake --build build --config Debug --target vsgocct`
Expected: Build succeeds.

- [ ] **Step 4: Commit**

```bash
git add include/vsgocct/StepModelLoader.h src/vsgocct/StepModelLoader.cpp
git commit -m "feat: update StepModelLoader facade to use AssemblySceneData"
```

---

### Task 9: Rewrite scene module tests

**Files:**
- Rewrite: `tests/test_scene_builder.cpp`

- [ ] **Step 1: Write the new test file**

Replace the entire content of `tests/test_scene_builder.cpp`:

```cpp
#include <gtest/gtest.h>

#include "test_helpers.h"

#include <vsgocct/cad/StepReader.h>
#include <vsgocct/scene/SceneBuilder.h>

#include <cmath>

using namespace vsgocct::cad;
using namespace vsgocct::scene;
using namespace vsgocct::test;

class AssemblySceneTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        assembly = readStep(testDataPath("nested_assembly.step"));
    }
    AssemblyData assembly;
};

TEST_F(AssemblySceneTest, BuildSceneNonNull)
{
    auto sceneData = buildAssemblyScene(assembly);
    EXPECT_TRUE(sceneData.scene);
}

TEST_F(AssemblySceneTest, PartsListMatchesLeafCount)
{
    auto sceneData = buildAssemblyScene(assembly);

    // nested_assembly has 2 Part leaves (Box + Cylinder)
    EXPECT_EQ(sceneData.parts.size(), 2u);
}

TEST_F(AssemblySceneTest, PartsHaveNames)
{
    auto sceneData = buildAssemblyScene(assembly);

    for (const auto& part : sceneData.parts)
    {
        EXPECT_FALSE(part.name.empty());
    }
}

TEST_F(AssemblySceneTest, PartSwitchTogglesVisibility)
{
    auto sceneData = buildAssemblyScene(assembly);
    ASSERT_FALSE(sceneData.parts.empty());

    auto& sw = sceneData.parts.front().switchNode;
    ASSERT_TRUE(sw);
    ASSERT_FALSE(sw->children.empty());

    // Default: visible
    EXPECT_NE(sw->children.front().mask, vsg::MASK_OFF);

    // Toggle off
    sw->children.front().mask = vsg::MASK_OFF;
    EXPECT_EQ(sw->children.front().mask, vsg::MASK_OFF);

    // Toggle on
    sw->children.front().mask = vsg::MASK_ALL;
    EXPECT_NE(sw->children.front().mask, vsg::MASK_OFF);
}

TEST_F(AssemblySceneTest, SceneCenterAndRadius)
{
    auto sceneData = buildAssemblyScene(assembly);
    EXPECT_FALSE(std::isnan(sceneData.center.x));
    EXPECT_FALSE(std::isnan(sceneData.center.y));
    EXPECT_FALSE(std::isnan(sceneData.center.z));
    EXPECT_GT(sceneData.radius, 0.0);
}

TEST_F(AssemblySceneTest, GeometryCounts)
{
    auto sceneData = buildAssemblyScene(assembly);
    EXPECT_GT(sceneData.totalTriangleCount, 0u);
    // Lines and points may or may not exist depending on meshing
}

TEST(AssemblySceneSimple, EmptyAssemblyRoots)
{
    // Empty assembly should produce valid but empty scene
    AssemblyData empty;
    // buildAssemblyScene should not crash on empty input
    // Note: readStep would throw before this happens, but test the scene builder directly
    auto sceneData = buildAssemblyScene(empty);
    EXPECT_TRUE(sceneData.scene);
    EXPECT_TRUE(sceneData.parts.empty());
}

TEST(AssemblySceneSimple, SinglePartBox)
{
    auto assembly = readStep(testDataPath("box.step"));
    auto sceneData = buildAssemblyScene(assembly);
    EXPECT_TRUE(sceneData.scene);
    EXPECT_GE(sceneData.parts.size(), 1u);
    EXPECT_GT(sceneData.totalTriangleCount, 0u);
}
```

- [ ] **Step 2: Build and run tests**

Run: `cmake --build build --config Debug --target test_scene_builder && build\tests\Debug\test_scene_builder.exe`
Expected: All tests PASS.

- [ ] **Step 3: Commit**

```bash
git add tests/test_scene_builder.cpp
git commit -m "test(scene): rewrite scene builder tests for AssemblySceneData"
```

---

## Chunk 3: Viewer Update and Final Integration

### Task 10: Update vsgqt_step_viewer example

**Files:**
- Modify: `examples/vsgqt_step_viewer/main.cpp`

- [ ] **Step 1: Update the viewer to use AssemblySceneData with part list UI**

Replace the entire content of `examples/vsgqt_step_viewer/main.cpp`:

```cpp
#include <vsgocct/StepModelLoader.h>

#include <QtCore/QFileInfo>
#include <QtCore/QStringList>
#include <QtWidgets/QApplication>
#include <QtWidgets/QCheckBox>
#include <QtWidgets/QFileDialog>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QMainWindow>
#include <QtWidgets/QMessageBox>
#include <QtWidgets/QScrollArea>
#include <QtWidgets/QStatusBar>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QWidget>

#include <vsgQt/Viewer.h>
#include <vsgQt/Window.h>

#include <algorithm>
#include <filesystem>
#include <iostream>

namespace
{
QString resolveStepFile(const QStringList& arguments)
{
    if (arguments.size() > 1)
    {
        return arguments.at(1);
    }

    const QString initialDirectory = std::filesystem::exists("D:/OCCT/data/step")
                                         ? QStringLiteral("D:/OCCT/data/step")
                                         : QString();

    return QFileDialog::getOpenFileName(
        nullptr,
        QStringLiteral("Open STEP Model"),
        initialDirectory,
        QStringLiteral("STEP Files (*.step *.stp *.STEP *.STP);;All Files (*.*)"));
}

vsgQt::Window* createRenderWindow(
    const vsg::ref_ptr<vsgQt::Viewer>& viewer,
    const vsg::ref_ptr<vsg::WindowTraits>& traits,
    const vsgocct::scene::AssemblySceneData& sceneData)
{
    auto* window = new vsgQt::Window(viewer, traits, static_cast<QWindow*>(nullptr));
    window->setTitle(QString::fromStdString(traits->windowTitle));
    window->initializeWindow();

    if (!traits->device)
    {
        traits->device = window->windowAdapter->getOrCreateDevice();
    }

    const auto width = traits->width;
    const auto height = traits->height;
    const auto radius = std::max(sceneData.radius, 1.0);
    const auto centre = sceneData.center;

    auto lookAt = vsg::LookAt::create(
        centre + vsg::dvec3(0.0, -radius * 3.0, radius * 1.6),
        centre,
        vsg::dvec3(0.0, 0.0, 1.0));
    auto projection = vsg::Perspective::create(
        35.0,
        static_cast<double>(width) / static_cast<double>(height),
        std::max(radius * 0.001, 0.001),
        radius * 12.0);
    auto camera = vsg::Camera::create(projection, lookAt, vsg::ViewportState::create(VkExtent2D{width, height}));

    auto trackball = vsg::Trackball::create(camera);
    trackball->addWindow(*window);

    viewer->addEventHandler(trackball);
    viewer->addEventHandler(vsg::CloseHandler::create(viewer));

    auto commandGraph = vsg::createCommandGraphForView(*window, camera, sceneData.scene);
    viewer->addRecordAndSubmitTaskAndPresentation({commandGraph});

    return window;
}

class OverlayPositioner : public QObject
{
public:
    OverlayPositioner(QWidget* overlay, QWidget* anchor, const QPoint& offset)
        : QObject(anchor), overlay_(overlay), anchor_(anchor), offset_(offset) {}

protected:
    bool eventFilter(QObject* /*watched*/, QEvent* event) override
    {
        switch (event->type())
        {
        case QEvent::Move:
        case QEvent::Resize:
        case QEvent::Show:
            overlay_->move(anchor_->mapToGlobal(offset_));
            break;
        case QEvent::WindowStateChange:
            if (anchor_->isMinimized())
                overlay_->hide();
            else if (anchor_->isVisible())
            {
                overlay_->show();
                overlay_->move(anchor_->mapToGlobal(offset_));
            }
            break;
        default:
            break;
        }
        return false;
    }

private:
    QWidget* overlay_;
    QWidget* anchor_;
    QPoint offset_;
};
} // namespace

int main(int argc, char* argv[])
{
    try
    {
        QApplication application(argc, argv);

        const QString stepFile = resolveStepFile(application.arguments());
        if (stepFile.isEmpty())
        {
            return 0;
        }

        auto sceneData = vsgocct::loadStepScene(std::filesystem::path(stepFile.toStdWString()));
        std::cout << "Loaded " << sceneData.totalPointCount << " points, "
                  << sceneData.totalLineSegmentCount << " line segments, "
                  << sceneData.totalTriangleCount << " triangles, "
                  << sceneData.parts.size() << " parts from "
                  << stepFile.toLocal8Bit().constData() << std::endl;

        auto viewer = vsgQt::Viewer::create();

        auto traits = vsg::WindowTraits::create();
        traits->windowTitle = "vsgQt OCCT STEP Viewer";
        traits->width = 1280;
        traits->height = 900;
        traits->samples = VK_SAMPLE_COUNT_4_BIT;

        QMainWindow mainWindow;
        auto* renderWindow = createRenderWindow(viewer, traits, sceneData);

        auto* centralBase = new QWidget(&mainWindow);
        auto* layout = new QVBoxLayout(centralBase);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(0);

        auto* container = QWidget::createWindowContainer(renderWindow);
        container->setFocusPolicy(Qt::StrongFocus);
        layout->addWidget(container);

        mainWindow.setWindowTitle(QStringLiteral("STEP Viewer - %1").arg(QFileInfo(stepFile).fileName()));
        mainWindow.setCentralWidget(centralBase);
        mainWindow.resize(static_cast<int>(traits->width), static_cast<int>(traits->height));

        // --- Floating overlay: part list with visibility toggles ---
        auto* overlay = new QWidget(&mainWindow, Qt::Tool | Qt::FramelessWindowHint);
        overlay->setAttribute(Qt::WA_TranslucentBackground);
        overlay->setStyleSheet(QStringLiteral(R"(
            QWidget#overlayPanel {
                background-color: rgba(30, 30, 30, 180);
                border-radius: 8px;
                border: 1px solid rgba(255, 255, 255, 30);
            }
            QCheckBox {
                color: rgba(255, 255, 255, 180);
                font-size: 12px;
                padding: 3px 6px;
            }
            QCheckBox::indicator {
                width: 14px;
                height: 14px;
            }
            QLabel#partListTitle {
                color: rgba(255, 255, 255, 120);
                font-size: 11px;
                font-weight: bold;
                padding: 2px 6px;
            }
        )"));

        auto* panel = new QWidget(overlay);
        panel->setObjectName(QStringLiteral("overlayPanel"));
        auto* overlayRoot = new QVBoxLayout(overlay);
        overlayRoot->setContentsMargins(0, 0, 0, 0);
        overlayRoot->addWidget(panel);

        auto* panelLayout = new QVBoxLayout(panel);
        panelLayout->setContentsMargins(8, 6, 8, 6);
        panelLayout->setSpacing(2);

        auto* title = new QLabel(QStringLiteral("Parts (%1)").arg(sceneData.parts.size()), panel);
        title->setObjectName(QStringLiteral("partListTitle"));
        panelLayout->addWidget(title);

        for (const auto& part : sceneData.parts)
        {
            QString label = QString::fromStdString(part.name);
            if (label.isEmpty())
            {
                label = QStringLiteral("(unnamed)");
            }

            auto* checkbox = new QCheckBox(label, panel);
            checkbox->setChecked(true);

            // Capture switchNode by value (ref_ptr copy is cheap)
            auto switchNode = part.switchNode;
            QObject::connect(checkbox, &QCheckBox::toggled, &mainWindow,
                [switchNode](bool visible)
                {
                    if (switchNode && !switchNode->children.empty())
                    {
                        switchNode->children.front().mask = vsg::boolToMask(visible);
                    }
                });

            panelLayout->addWidget(checkbox);
        }

        overlay->setMaximumHeight(400);
        overlay->adjustSize();

        const QPoint overlayOffset(12, 12);
        mainWindow.installEventFilter(new OverlayPositioner(overlay, &mainWindow, overlayOffset));

        mainWindow.statusBar()->showMessage(
            QStringLiteral("Parts: %1 | Triangles: %2 | Lines: %3 | Points: %4")
                .arg(static_cast<qulonglong>(sceneData.parts.size()))
                .arg(static_cast<qulonglong>(sceneData.totalTriangleCount))
                .arg(static_cast<qulonglong>(sceneData.totalLineSegmentCount))
                .arg(static_cast<qulonglong>(sceneData.totalPointCount)));

        viewer->continuousUpdate = true;
        viewer->setInterval(16);
        viewer->compile();

        mainWindow.show();

        overlay->move(mainWindow.mapToGlobal(overlayOffset));
        overlay->show();

        return application.exec();
    }
    catch (const vsg::Exception& ex)
    {
        std::cerr << "VSG exception: " << ex.message << " (result=" << ex.result << ')' << std::endl;
        QMessageBox::critical(nullptr, QStringLiteral("VSG Error"), QString::fromLocal8Bit(ex.message.c_str()));
        return 1;
    }
    catch (const std::exception& ex)
    {
        std::cerr << "Unhandled exception: " << ex.what() << std::endl;
        QMessageBox::critical(nullptr, QStringLiteral("STEP Viewer Error"), QString::fromLocal8Bit(ex.what()));
        return 1;
    }
}
```

Key changes from original:
- `StepSceneData` replaced with `scene::AssemblySceneData`
- Points/Lines/Faces toggle buttons replaced with per-part `QCheckBox` list
- Status bar shows part count instead of per-primitive-type counts
- `QPushButton` styling replaced with `QCheckBox` styling
- Part list title shows total count

- [ ] **Step 2: Build the viewer**

Run: `cmake --build build --config Debug --target vsgqt_step_viewer`
Expected: Build succeeds.

- [ ] **Step 3: Commit**

```bash
git add examples/vsgqt_step_viewer/main.cpp
git commit -m "feat(viewer): update to AssemblySceneData with per-part visibility toggles"
```

---

### Task 11: Full integration test

- [ ] **Step 1: Clean build from scratch**

Run: `cmake --build build --config Debug --clean-first`
Expected: Full build succeeds with zero errors.

- [ ] **Step 2: Regenerate test data**

Run: `build\tests\Debug\generate_test_data.exe`
Expected: All 5 STEP files generated.

- [ ] **Step 3: Run all tests**

Run: `ctest --test-dir build --build-config Debug --output-on-failure`
Expected: All tests PASS.

- [ ] **Step 4: Manual smoke test (viewer)**

Run: `build\examples\vsgqt_step_viewer\Debug\vsgqt_step_viewer.exe` with `nested_assembly.step`
Verify:
- Model loads and renders with correct colors (blue box, orange cylinder)
- Floating panel shows part list with checkboxes
- Toggling checkboxes hides/shows individual parts
- Status bar shows part count and triangle count
- Camera framing and navigation work correctly

- [ ] **Step 5: Commit any final fixes if needed**
