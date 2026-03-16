# Face-Level Color-Coded Picking Implementation Plan

> **For agentic workers:** REQUIRED: Use superpowers:subagent-driven-development (if subagents available) or superpowers:executing-plans to implement this plan. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add face-level picking to vsgOcct — click a face in the 3D viewer and get its ID, highlight, and metadata panel.

**Architecture:** Color-coded offscreen rendering. Each TopoDS_Face gets a unique uint32 ID encoded as RGB. A second render pass writes ID colors to an offscreen framebuffer every frame. On click, the pixel is read back, decoded to a face ID, and used to drive highlight + info panel. The main and pick scene graphs share a single Switch per part using VSG traversal mask bits for selection — `MASK_ALL` main child renders in the main pass, `MASK_ALL` pick child renders in the pick pass. Each `vsg::View` is configured with a traversal mask to select the appropriate child.

**Tech Stack:** C++17, VulkanSceneGraph (VSG) 1.1.2, OpenCASCADE (OCCT), Qt 5/6, GoogleTest

**Spec:** `docs/superpowers/specs/2026-03-16-face-picking-design.md`

**VSG readback reference:** Consult the `vsgscreenshot` example in the VSG repository for the canonical pattern for GPU→CPU image readback. Key VSG types: `vsg::CopyImageToBuffer`, `vsg::Commands`, `vsg::Fence`, `vsg::MemoryBufferPools`.

---

## File Structure

### New files
| File | Responsibility |
|------|---------------|
| `include/vsgocct/pick/FaceIdCodec.h` | `encodeFaceId()` / `decodeFaceId()` — pure functions, no dependencies |
| `include/vsgocct/pick/PickHandler.h` | Event handler: mouse capture, pixel readback, callback dispatch |
| `src/vsgocct/pick/PickHandler.cpp` | Implementation of PickHandler |
| `include/vsgocct/pick/SelectionManager.h` | Highlight state: select/deselect face, color buffer manipulation |
| `src/vsgocct/pick/SelectionManager.cpp` | Implementation of SelectionManager |
| `tests/test_face_id_codec.cpp` | Unit tests for ID encoding/decoding |

### Modified files
| File | Changes |
|------|---------|
| `include/vsgocct/mesh/ShapeMesher.h` | Add `#include <cstdint>`, `FaceData` struct, `faces` + `perTriangleFaceId` to `MeshResult`, `nextFaceId` param overload |
| `src/vsgocct/mesh/ShapeMesher.cpp` | Populate `FaceData` and `perTriangleFaceId` in `extractFaces()` |
| `include/vsgocct/scene/SceneBuilder.h` | Add `FaceInfo`, `PickResult`, `PartColorRef`, extend `AssemblySceneData` with `pickScene`, `faceRegistry`, `faceColorRefs` |
| `src/vsgocct/scene/SceneBuilder.cpp` | Pass `nextFaceId` counter, create ID color buffer, populate `faceRegistry` + `faceColorRefs`, add pick shaders + pick scene graph, create dedicated `createPickPipeline()` |
| `src/vsgocct/CMakeLists.txt` | Add new source files to library |
| `tests/CMakeLists.txt` | Add new test targets |
| `tests/test_shape_mesher.cpp` | Add tests for FaceData population |
| `tests/test_scene_builder.cpp` | Add tests for face registry and pick scene |
| `examples/vsgqt_step_viewer/main.cpp` | Wire PickHandler, SelectionManager, offscreen framebuffer, info panel |

---

## Chunk 1: Face ID Codec and Mesh FaceData

### Task 1: FaceIdCodec — pure encoding/decoding utility

**Files:**
- Create: `include/vsgocct/pick/FaceIdCodec.h`
- Create: `tests/test_face_id_codec.cpp`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Write failing tests for FaceIdCodec**

Create `tests/test_face_id_codec.cpp`:

```cpp
#include <gtest/gtest.h>

#include <vsgocct/pick/FaceIdCodec.h>

using namespace vsgocct::pick;

TEST(FaceIdCodec, EncodeDecodeRoundTrip)
{
    for (uint32_t id : {1u, 2u, 255u, 256u, 65535u, 65536u, 16777215u})
    {
        auto color = encodeFaceId(id);
        EXPECT_EQ(decodeFaceId(color), id) << "Failed round-trip for id=" << id;
    }
}

TEST(FaceIdCodec, BackgroundIsZero)
{
    auto color = encodeFaceId(0);
    EXPECT_FLOAT_EQ(color.x, 0.0f);
    EXPECT_FLOAT_EQ(color.y, 0.0f);
    EXPECT_FLOAT_EQ(color.z, 0.0f);

    EXPECT_EQ(decodeFaceId(vsg::vec3(0.0f, 0.0f, 0.0f)), 0u);
}

TEST(FaceIdCodec, EncodeId1)
{
    auto color = encodeFaceId(1);
    // id=1 → R=0, G=0, B=1 → (0/255, 0/255, 1/255)
    EXPECT_FLOAT_EQ(color.x, 0.0f);
    EXPECT_FLOAT_EQ(color.y, 0.0f);
    EXPECT_NEAR(color.z, 1.0f / 255.0f, 1e-6f);
}

TEST(FaceIdCodec, EncodeMaxId)
{
    // 24-bit max = 16777215
    auto color = encodeFaceId(16777215u);
    EXPECT_NEAR(color.x, 1.0f, 1e-6f);
    EXPECT_NEAR(color.y, 1.0f, 1e-6f);
    EXPECT_NEAR(color.z, 1.0f, 1e-6f);
}

TEST(FaceIdCodec, DecodeFromUint8)
{
    // Simulate reading pixel bytes (R=0, G=1, B=42)
    EXPECT_EQ(decodeFaceIdFromBytes(0, 1, 42), (1u << 8) | 42u);
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build --target test_face_id_codec && ctest --test-dir build -R FaceIdCodec -V`
Expected: Build fails — header does not exist yet.

- [ ] **Step 3: Create FaceIdCodec header**

Create `include/vsgocct/pick/FaceIdCodec.h`:

```cpp
#pragma once

#include <cstdint>

#include <vsg/maths/vec3.h>

namespace vsgocct::pick
{
inline vsg::vec3 encodeFaceId(uint32_t faceId)
{
    uint8_t r = static_cast<uint8_t>((faceId >> 16) & 0xFF);
    uint8_t g = static_cast<uint8_t>((faceId >> 8) & 0xFF);
    uint8_t b = static_cast<uint8_t>(faceId & 0xFF);
    return vsg::vec3(r / 255.0f, g / 255.0f, b / 255.0f);
}

inline uint32_t decodeFaceId(const vsg::vec3& color)
{
    auto r = static_cast<uint8_t>(color.x * 255.0f + 0.5f);
    auto g = static_cast<uint8_t>(color.y * 255.0f + 0.5f);
    auto b = static_cast<uint8_t>(color.z * 255.0f + 0.5f);
    return (static_cast<uint32_t>(r) << 16) | (static_cast<uint32_t>(g) << 8) | b;
}

inline uint32_t decodeFaceIdFromBytes(uint8_t r, uint8_t g, uint8_t b)
{
    return (static_cast<uint32_t>(r) << 16) | (static_cast<uint32_t>(g) << 8) | b;
}
} // namespace vsgocct::pick
```

- [ ] **Step 4: Add test target to CMakeLists**

In `tests/CMakeLists.txt`, add after the existing test targets:

```cmake
vsgocct_add_test(test_face_id_codec)
```

- [ ] **Step 5: Run test to verify it passes**

Run: `cmake --build build --target test_face_id_codec && ctest --test-dir build -R FaceIdCodec -V`
Expected: All 5 tests PASS.

- [ ] **Step 6: Commit**

```bash
git add include/vsgocct/pick/FaceIdCodec.h tests/test_face_id_codec.cpp tests/CMakeLists.txt
git commit -m "feat(pick): add FaceIdCodec for face ID ↔ RGB encoding"
```

---

### Task 2: Add FaceData to MeshResult and populate during triangulation

**Files:**
- Modify: `include/vsgocct/mesh/ShapeMesher.h`
- Modify: `src/vsgocct/mesh/ShapeMesher.cpp`
- Modify: `tests/test_shape_mesher.cpp`

- [ ] **Step 1: Write failing tests for FaceData population**

Add to `tests/test_shape_mesher.cpp`:

```cpp
TEST_F(ShapeMesherTest, TriangulateWithFaceIds)
{
    uint32_t nextFaceId = 1;
    auto result = triangulate(boxShape, nextFaceId);

    // A box has 6 faces
    EXPECT_EQ(result.faces.size(), 6u);

    // nextFaceId should have advanced by 6
    EXPECT_EQ(nextFaceId, 7u);

    // Every face should have a non-zero faceId
    for (const auto& face : result.faces)
    {
        EXPECT_GT(face.faceId, 0u);
        EXPECT_GT(face.triangleCount, 0u);
    }
}

TEST_F(ShapeMesherTest, PerTriangleFaceIdParallelToTriangles)
{
    uint32_t nextFaceId = 1;
    auto result = triangulate(boxShape, nextFaceId);

    EXPECT_EQ(result.perTriangleFaceId.size(), result.triangleCount);

    for (auto id : result.perTriangleFaceId)
    {
        EXPECT_GE(id, 1u);
        EXPECT_LT(id, nextFaceId);
    }
}

TEST_F(ShapeMesherTest, FaceDataTriangleOffsetsAreConsistent)
{
    uint32_t nextFaceId = 1;
    auto result = triangulate(boxShape, nextFaceId);

    uint32_t totalFromFaces = 0;
    for (const auto& face : result.faces)
    {
        EXPECT_EQ(face.triangleOffset, totalFromFaces);
        totalFromFaces += face.triangleCount;
    }
    EXPECT_EQ(totalFromFaces, static_cast<uint32_t>(result.triangleCount));
}

TEST_F(ShapeMesherTest, GlobalFaceIdContinuesAcrossCalls)
{
    uint32_t nextFaceId = 1;
    auto result1 = triangulate(boxShape, nextFaceId);
    uint32_t afterFirst = nextFaceId;

    auto result2 = triangulate(boxShape, nextFaceId);
    EXPECT_EQ(result2.faces.front().faceId, afterFirst);
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build --target test_shape_mesher`
Expected: Build fails — `triangulate()` does not accept `uint32_t&` as second parameter.

- [ ] **Step 3: Update ShapeMesher.h**

Add `#include <cstdint>` to the includes.

Add `FaceData` struct after `MeshOptions`:

```cpp
struct FaceData
{
    uint32_t faceId = 0;
    uint32_t triangleOffset = 0;
    uint32_t triangleCount = 0;
    vsg::vec3 normal{0.0f, 0.0f, 0.0f};
};
```

Add to `MeshResult` (after `boundsMax`):

```cpp
std::vector<FaceData> faces;
std::vector<uint32_t> perTriangleFaceId;
```

Add the new `triangulate` overload:

```cpp
MeshResult triangulate(const TopoDS_Shape& shape, uint32_t& nextFaceId,
                       const MeshOptions& options = {});
```

Keep the old signature as an inline overload (no ambiguity — `uint32_t&` cannot be implicitly constructed from `MeshOptions`, and `MeshOptions` cannot be implicitly constructed from `uint32_t&`; all existing call sites continue to work):

```cpp
inline MeshResult triangulate(const TopoDS_Shape& shape,
                              const MeshOptions& options = {})
{
    uint32_t nextFaceId = 1;
    return triangulate(shape, nextFaceId, options);
}
```

- [ ] **Step 4: Update ShapeMesher.cpp to populate FaceData**

Add `faces` and `perTriangleFaceId` fields to the internal `FaceBuffers` struct:

```cpp
struct FaceBuffers
{
    std::vector<vsg::vec3> positions;
    std::vector<vsg::vec3> normals;
    std::size_t triangleCount = 0;
    std::vector<FaceData> faces;
    std::vector<uint32_t> perTriangleFaceId;
};
```

Update `extractFaces()` signature to accept `uint32_t& nextFaceId`.

Inside `extractFaces()`, at the start of each face iteration (after the triangulation null check), add:

```cpp
uint32_t currentFaceId = nextFaceId++;
uint32_t faceTriangleOffset = static_cast<uint32_t>(buffers.faces.triangleCount);
uint32_t faceTriangleCount = 0;
double nx = 0.0, ny = 0.0, nz = 0.0;
```

Inside the triangle loop, after `++buffers.faces.triangleCount;`, add:

```cpp
++faceTriangleCount;
buffers.faces.perTriangleFaceId.push_back(currentFaceId);
nx += faceNormal.X();
ny += faceNormal.Y();
nz += faceNormal.Z();
```

After the triangle loop (still inside the face iteration), add:

```cpp
if (faceTriangleCount > 0)
{
    vsg::vec3 avgNormal(
        static_cast<float>(nx / faceTriangleCount),
        static_cast<float>(ny / faceTriangleCount),
        static_cast<float>(nz / faceTriangleCount));
    float len = vsg::length(avgNormal);
    if (len > 0.0f) avgNormal = avgNormal / len;

    buffers.faces.faces.push_back({currentFaceId, faceTriangleOffset, faceTriangleCount, avgNormal});
}
```

Change old `triangulate()` to the new signature `MeshResult triangulate(const TopoDS_Shape& shape, uint32_t& nextFaceId, const MeshOptions& options)`, pass `nextFaceId` to `extractFaces()`, and copy the new fields into `MeshResult`:

```cpp
result.faces = std::move(buffers.faces.faces);
result.perTriangleFaceId = std::move(buffers.faces.perTriangleFaceId);
```

- [ ] **Step 5: Run all mesher tests to verify**

Run: `cmake --build build --target test_shape_mesher && ctest --test-dir build -R ShapeMesher -V`
Expected: All tests PASS, including the 4 new ones. Existing tests use the old overload (no `nextFaceId` param) which calls the new one internally.

- [ ] **Step 6: Commit**

```bash
git add include/vsgocct/mesh/ShapeMesher.h src/vsgocct/mesh/ShapeMesher.cpp tests/test_shape_mesher.cpp
git commit -m "feat(mesh): add FaceData and per-triangle face ID tracking to MeshResult"
```

---

## Chunk 2: Scene Builder — FaceRegistry, Pick Scene Graph, and ID Color Buffers

### Task 3: Extend AssemblySceneData and build parallel pick scene graph

**Files:**
- Modify: `include/vsgocct/scene/SceneBuilder.h`
- Modify: `src/vsgocct/scene/SceneBuilder.cpp`
- Modify: `tests/test_scene_builder.cpp`

- [ ] **Step 1: Write failing tests for face registry and pick scene**

Add to `tests/test_scene_builder.cpp`:

```cpp
TEST_F(AssemblySceneTest, FaceRegistryPopulated)
{
    auto sceneData = buildAssemblyScene(assembly);
    EXPECT_FALSE(sceneData.faceRegistry.empty());

    for (const auto& [faceId, info] : sceneData.faceRegistry)
    {
        EXPECT_GT(faceId, 0u);
        EXPECT_FALSE(info.partName.empty());
    }
}

TEST_F(AssemblySceneTest, FaceColorRefsPopulated)
{
    auto sceneData = buildAssemblyScene(assembly);
    EXPECT_FALSE(sceneData.faceColorRefs.empty());

    for (const auto& [faceId, info] : sceneData.faceRegistry)
    {
        EXPECT_TRUE(sceneData.faceColorRefs.count(faceId) > 0)
            << "Missing color ref for faceId=" << faceId;
        auto& ref = sceneData.faceColorRefs.at(faceId);
        EXPECT_TRUE(ref.colorArray);
        EXPECT_GT(ref.vertexCount, 0u);
    }
}

TEST_F(AssemblySceneTest, PickSceneCreated)
{
    auto sceneData = buildAssemblyScene(assembly);
    EXPECT_TRUE(sceneData.pickScene);
}

TEST(AssemblySceneSimple, SinglePartBoxFaceRegistry)
{
    auto assembly = readStep(testDataPath("box.step"));
    auto sceneData = buildAssemblyScene(assembly);

    // A box has 6 faces
    EXPECT_EQ(sceneData.faceRegistry.size(), 6u);
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build build --target test_scene_builder`
Expected: Build fails — `faceRegistry`, `faceColorRefs`, `pickScene` don't exist yet.

- [ ] **Step 3: Update SceneBuilder.h with new types and fields**

Add `#include <unordered_map>` to includes.

Add after `PartSceneNode`:

```cpp
struct FaceInfo
{
    uint32_t faceId = 0;
    std::string partName;
    uint32_t partIndex = 0;
    vsg::vec3 faceNormal{0.0f, 0.0f, 0.0f};
};

struct PickResult
{
    uint32_t faceId = 0;
    const FaceInfo* faceInfo = nullptr;
};

struct PartColorRef
{
    vsg::ref_ptr<vsg::vec3Array> colorArray;
    uint32_t vertexOffset = 0;
    uint32_t vertexCount = 0;
};
```

Add to `AssemblySceneData` (after `totalPointCount`):

```cpp
vsg::ref_ptr<vsg::Node> pickScene;
std::unordered_map<uint32_t, FaceInfo> faceRegistry;
std::unordered_map<uint32_t, PartColorRef> faceColorRefs;
```

- [ ] **Step 4: Add pick shaders and dedicated pick pipeline to SceneBuilder.cpp**

Add pick shader strings after the existing shader strings:

```cpp
constexpr const char* PICK_VERT_SHADER = R"(
#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(push_constant) uniform PushConstants
{
    mat4 projection;
    mat4 modelView;
};

layout(location = 0) in vec3 vertex;
layout(location = 1) in vec3 idColor;

layout(location = 0) out vec3 fragIdColor;

out gl_PerVertex
{
    vec4 gl_Position;
};

void main()
{
    gl_Position = projection * modelView * vec4(vertex, 1.0);
    fragIdColor = idColor;
}
)";

constexpr const char* PICK_FRAG_SHADER = R"(
#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(location = 0) in vec3 fragIdColor;
layout(location = 0) out vec4 outColor;

void main()
{
    outColor = vec4(fragIdColor, 1.0);
}
)";
```

Create a dedicated `createPickPipeline()` function (separate from `createPrimitivePipeline`) because the pick pipeline needs vertex position at binding 0 / location 0 and idColor at binding 1 / location 1 — different from the main pipeline where color is always at attribute location 2:

```cpp
vsg::ref_ptr<vsg::BindGraphicsPipeline> createPickPipeline()
{
    vsg::DescriptorSetLayouts descriptorSetLayouts;
    vsg::PushConstantRanges pushConstantRanges{
        {VK_SHADER_STAGE_VERTEX_BIT, 0, 128}};
    auto pipelineLayout = vsg::PipelineLayout::create(descriptorSetLayouts, pushConstantRanges);

    auto vertexShader = vsg::ShaderStage::create(
        VK_SHADER_STAGE_VERTEX_BIT, "main", PICK_VERT_SHADER, vsg::ShaderCompileSettings::create());
    auto fragmentShader = vsg::ShaderStage::create(
        VK_SHADER_STAGE_FRAGMENT_BIT, "main", PICK_FRAG_SHADER, vsg::ShaderCompileSettings::create());
    auto shaderStages = vsg::ShaderStages{vertexShader, fragmentShader};

    auto vertexInputState = vsg::VertexInputState::create();
    auto& bindings = vertexInputState->vertexBindingDescriptions;
    auto& attributes = vertexInputState->vertexAttributeDescriptions;

    constexpr uint32_t offset = 0;
    // Binding 0: position → location 0
    bindings.emplace_back(VkVertexInputBindingDescription{0, sizeof(vsg::vec3), VK_VERTEX_INPUT_RATE_VERTEX});
    attributes.emplace_back(VkVertexInputAttributeDescription{0, 0, VK_FORMAT_R32G32B32_SFLOAT, offset});
    // Binding 1: idColor → location 1
    bindings.emplace_back(VkVertexInputBindingDescription{1, sizeof(vsg::vec3), VK_VERTEX_INPUT_RATE_VERTEX});
    attributes.emplace_back(VkVertexInputAttributeDescription{1, 1, VK_FORMAT_R32G32B32_SFLOAT, offset});

    auto inputAssemblyState = vsg::InputAssemblyState::create();
    inputAssemblyState->topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    auto rasterizationState = vsg::RasterizationState::create();
    rasterizationState->cullMode = VK_CULL_MODE_NONE;

    auto depthStencilState = vsg::DepthStencilState::create();
    depthStencilState->depthTestEnable = VK_TRUE;
    depthStencilState->depthWriteEnable = VK_TRUE;
    depthStencilState->depthCompareOp = VK_COMPARE_OP_GREATER_OR_EQUAL;

    auto graphicsPipelineStates = vsg::GraphicsPipelineStates{
        vertexInputState, inputAssemblyState, rasterizationState,
        vsg::ColorBlendState::create(), vsg::MultisampleState::create(), depthStencilState};

    auto graphicsPipeline = vsg::GraphicsPipeline::create(pipelineLayout, shaderStages, graphicsPipelineStates);
    return vsg::BindGraphicsPipeline::create(graphicsPipeline);
}
```

- [ ] **Step 5: Refactor createFaceNode to return shared arrays**

```cpp
struct FaceNodeResult
{
    vsg::ref_ptr<vsg::Node> node;
    vsg::ref_ptr<vsg::vec3Array> positions;
    vsg::ref_ptr<vsg::uintArray> indices;
    vsg::ref_ptr<vsg::vec3Array> colorArray;
};

FaceNodeResult createFaceNode(
    const std::vector<vsg::vec3>& facePositions,
    const std::vector<vsg::vec3>& faceNormals,
    const vsg::vec3& color)
{
    if (facePositions.empty())
    {
        return {vsg::Group::create(), {}, {}, {}};
    }

    auto vertexCount = static_cast<uint32_t>(facePositions.size());
    auto positions = vsg::vec3Array::create(vertexCount);
    auto normals = vsg::vec3Array::create(vertexCount);
    auto colors = vsg::vec3Array::create(vertexCount);
    auto indices = vsg::uintArray::create(vertexCount);

    for (std::size_t index = 0; index < facePositions.size(); ++index)
    {
        auto i = static_cast<uint32_t>(index);
        (*positions)[i] = facePositions[index];
        (*normals)[i] = faceNormals[index];
        (*colors)[i] = color;
        (*indices)[i] = i;
    }

    auto drawCommands = vsg::VertexIndexDraw::create();
    drawCommands->assignArrays(vsg::DataList{positions, normals, colors});
    drawCommands->assignIndices(indices);
    drawCommands->indexCount = indices->width();
    drawCommands->instanceCount = 1;

    auto facePipeline = createPrimitivePipeline(
        FACE_VERT_SHADER, FACE_FRAG_SHADER,
        VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
        true, true, true);

    auto stateGroup = vsg::StateGroup::create();
    stateGroup->add(facePipeline);
    stateGroup->addChild(drawCommands);

    return {stateGroup, positions, indices, colors};
}
```

- [ ] **Step 6: Add createPickFaceNode function**

Add `#include <vsgocct/pick/FaceIdCodec.h>` to SceneBuilder.cpp.

```cpp
vsg::ref_ptr<vsg::Node> createPickFaceNode(
    const vsg::ref_ptr<vsg::vec3Array>& positions,
    const vsg::ref_ptr<vsg::uintArray>& indices,
    const std::vector<uint32_t>& perTriangleFaceId,
    uint32_t vertexCount)
{
    if (vertexCount == 0)
    {
        return vsg::Group::create();
    }

    auto idColors = vsg::vec3Array::create(vertexCount);
    for (uint32_t tri = 0; tri < static_cast<uint32_t>(perTriangleFaceId.size()); ++tri)
    {
        auto color = pick::encodeFaceId(perTriangleFaceId[tri]);
        (*idColors)[tri * 3 + 0] = color;
        (*idColors)[tri * 3 + 1] = color;
        (*idColors)[tri * 3 + 2] = color;
    }

    auto drawCommands = vsg::VertexIndexDraw::create();
    drawCommands->assignArrays(vsg::DataList{positions, idColors});
    drawCommands->assignIndices(indices);
    drawCommands->indexCount = indices->width();
    drawCommands->instanceCount = 1;

    auto pickPipeline = createPickPipeline();

    auto stateGroup = vsg::StateGroup::create();
    stateGroup->add(pickPipeline);
    stateGroup->addChild(drawCommands);
    return stateGroup;
}
```

- [ ] **Step 7: Update buildNodeSubgraph to build pick scene and populate registry**

Add parameters to `buildNodeSubgraph`:

```cpp
void buildNodeSubgraph(
    const cad::ShapeNode& shapeNode,
    const vsg::ref_ptr<vsg::Group>& parentGroup,
    const vsg::ref_ptr<vsg::Group>& pickParentGroup,  // NEW
    const TopLoc_Location& accumulatedLocation,
    std::vector<PartSceneNode>& parts,
    BoundsAccumulator& bounds,
    const mesh::MeshOptions& meshOptions,
    std::size_t& totalTriangles,
    std::size_t& totalLines,
    std::size_t& totalPoints,
    uint32_t& nextFaceId,                                          // NEW
    std::unordered_map<uint32_t, FaceInfo>& faceRegistry,          // NEW
    std::unordered_map<uint32_t, PartColorRef>& faceColorRefs)     // NEW
```

In the Assembly branch, create a pick group and recurse with both:

```cpp
if (shapeNode.type == cad::ShapeNodeType::Assembly)
{
    auto group = vsg::Group::create();
    auto pickGroup = vsg::Group::create();
    for (const auto& child : shapeNode.children)
    {
        buildNodeSubgraph(child, group, pickGroup, currentLocation, parts, bounds,
                          meshOptions, totalTriangles, totalLines, totalPoints,
                          nextFaceId, faceRegistry, faceColorRefs);
    }
    parentGroup->addChild(group);
    pickParentGroup->addChild(pickGroup);
}
```

In the Part branch:

```cpp
else // Part
{
    TopoDS_Shape locatedShape = shapeNode.shape.Located(currentLocation);
    auto meshResult = mesh::triangulate(locatedShape, nextFaceId, meshOptions);

    auto color = resolveColor(shapeNode.color);
    auto faceResult = createFaceNode(meshResult.facePositions, meshResult.faceNormals, color);
    auto pickFaceNode = createPickFaceNode(
        faceResult.positions, faceResult.indices,
        meshResult.perTriangleFaceId,
        static_cast<uint32_t>(meshResult.facePositions.size()));

    auto lineNode = createPositionOnlyNode(/* ... unchanged ... */);
    auto pointNode = createPositionOnlyNode(/* ... unchanged ... */);

    // Main scene subtree
    auto partGroup = vsg::Group::create();
    partGroup->addChild(faceResult.node);
    partGroup->addChild(lineNode);
    partGroup->addChild(pointNode);

    auto partSwitch = vsg::Switch::create();
    partSwitch->addChild(true, partGroup);
    parentGroup->addChild(partSwitch);

    // Pick scene subtree — separate switch for independent visibility control
    auto pickPartGroup = vsg::Group::create();
    pickPartGroup->addChild(pickFaceNode);

    auto pickSwitch = vsg::Switch::create();
    pickSwitch->addChild(true, pickPartGroup);
    pickParentGroup->addChild(pickSwitch);

    // Populate face registry and color refs
    uint32_t partIdx = static_cast<uint32_t>(parts.size());
    for (const auto& faceData : meshResult.faces)
    {
        FaceInfo info;
        info.faceId = faceData.faceId;
        info.partName = shapeNode.name;
        info.partIndex = partIdx;
        info.faceNormal = faceData.normal;
        faceRegistry[faceData.faceId] = info;

        PartColorRef colorRef;
        colorRef.colorArray = faceResult.colorArray;
        colorRef.vertexOffset = faceData.triangleOffset * 3;
        colorRef.vertexCount = faceData.triangleCount * 3;
        faceColorRefs[faceData.faceId] = colorRef;
    }

    parts.push_back({shapeNode.name, partSwitch, pickSwitch});

    // ... rest unchanged (totalTriangles, bounds, etc.)
}
```

Update `PartSceneNode` in the header to include the pick switch:

```cpp
struct PartSceneNode
{
    std::string name;
    vsg::ref_ptr<vsg::Switch> switchNode;
    vsg::ref_ptr<vsg::Switch> pickSwitchNode;
};
```

- [ ] **Step 8: Update buildAssemblyScene to wire everything**

```cpp
AssemblySceneData buildAssemblyScene(
    const cad::AssemblyData& assembly,
    const mesh::MeshOptions& meshOptions,
    const SceneOptions& /*sceneOptions*/)
{
    auto root = vsg::Group::create();
    auto pickRoot = vsg::Group::create();
    std::vector<PartSceneNode> parts;
    BoundsAccumulator bounds;
    std::size_t totalTriangles = 0, totalLines = 0, totalPoints = 0;
    uint32_t nextFaceId = 1;
    std::unordered_map<uint32_t, FaceInfo> faceRegistry;
    std::unordered_map<uint32_t, PartColorRef> faceColorRefs;

    TopLoc_Location identity;
    for (const auto& rootNode : assembly.roots)
    {
        buildNodeSubgraph(rootNode, root, pickRoot, identity, parts, bounds,
                          meshOptions, totalTriangles, totalLines, totalPoints,
                          nextFaceId, faceRegistry, faceColorRefs);
    }

    AssemblySceneData sceneData;
    sceneData.scene = root;
    sceneData.pickScene = pickRoot;
    sceneData.parts = std::move(parts);
    // ... bounds, radius, center — unchanged ...
    sceneData.totalTriangleCount = totalTriangles;
    sceneData.totalLineSegmentCount = totalLines;
    sceneData.totalPointCount = totalPoints;
    sceneData.faceRegistry = std::move(faceRegistry);
    sceneData.faceColorRefs = std::move(faceColorRefs);

    return sceneData;
}
```

- [ ] **Step 9: Verify existing tests still pass after PartSceneNode change**

The existing test `PartSwitchTogglesVisibility` accesses `parts.front().switchNode` — this field still exists in the updated `PartSceneNode`. All existing tests should continue to work.

Run: `cmake --build build --target test_scene_builder && ctest --test-dir build -R AssemblyScene -V`
Expected: All tests PASS, including the 4 new ones.

- [ ] **Step 10: Commit**

```bash
git add include/vsgocct/scene/SceneBuilder.h src/vsgocct/scene/SceneBuilder.cpp tests/test_scene_builder.cpp
git commit -m "feat(scene): build pick scene graph, face registry, and faceColorRefs"
```

---

## Chunk 3: PickHandler and SelectionManager

### Task 4: PickHandler — event handling and pixel readback

**Files:**
- Create: `include/vsgocct/pick/PickHandler.h`
- Create: `src/vsgocct/pick/PickHandler.cpp`
- Modify: `src/vsgocct/CMakeLists.txt`

**Important**: The pixel readback implementation below is a structural skeleton. The exact VSG API for command buffer allocation, buffer creation, and queue submission varies between VSG versions. During implementation, consult the `vsgscreenshot` example in the VSG repository (specifically `vsgscreenshot.cpp`) for the canonical approach to `CopyImageToBuffer` and fence-based synchronization. The flow (record barrier → copy → barrier → submit → wait → read) is correct; the specific VSG wrapper calls may need adaptation.

- [ ] **Step 1: Create PickHandler header**

Create `include/vsgocct/pick/PickHandler.h`:

```cpp
#pragma once

#include <cstdint>
#include <functional>

#include <vsg/all.h>

#include <vsgocct/pick/FaceIdCodec.h>
#include <vsgocct/scene/SceneBuilder.h>

namespace vsgocct::pick
{
using PickCallback = std::function<void(const scene::PickResult& result)>;

class PickHandler : public vsg::Inherit<vsg::Visitor, PickHandler>
{
public:
    PickHandler(
        vsg::ref_ptr<vsg::Image> idImage,
        vsg::ref_ptr<vsg::Device> device,
        const scene::AssemblySceneData& sceneData);

    void setPickCallback(PickCallback callback);

    void apply(vsg::ButtonPressEvent& event) override;
    void apply(vsg::FrameEvent& event) override;

private:
    void performReadback();

    PickCallback _callback;
    vsg::ref_ptr<vsg::Image> _idImage;
    vsg::ref_ptr<vsg::Device> _device;
    const scene::AssemblySceneData& _sceneData;

    bool _pendingPick = false;
    int32_t _pickX = 0;
    int32_t _pickY = 0;
    uint32_t _imageWidth = 0;
    uint32_t _imageHeight = 0;
};
} // namespace vsgocct::pick
```

- [ ] **Step 2: Create PickHandler implementation**

Create `src/vsgocct/pick/PickHandler.cpp`. The implementation should:

1. In the constructor: store references, extract image dimensions, set up any reusable readback resources (staging buffer, command pool).
2. `apply(ButtonPressEvent&)`: On left button press, record `_pickX`, `_pickY`, set `_pendingPick = true`.
3. `apply(FrameEvent&)`: If `_pendingPick`, call `performReadback()` and clear the flag.
4. `performReadback()`: Bounds-check coordinates, then execute the readback sequence:
   - Record a one-shot command buffer: image layout transition → `CopyImageToBuffer` (1x1 pixel at _pickX,_pickY) → image layout transition back
   - Submit with fence, wait for fence
   - Map staging buffer, read RGBA bytes
   - Decode via `decodeFaceIdFromBytes(r, g, b)`
   - Look up in `_sceneData.faceRegistry`
   - Invoke `_callback` with the `PickResult`

**Refer to `vsgscreenshot` example** for the exact VSG calls for:
- Creating a staging `vsg::Buffer` with `VK_BUFFER_USAGE_TRANSFER_DST_BIT`
- Using `vsg::ImageMemoryBarrier` and `vsg::PipelineBarrier` (VSG wrappers, not raw Vulkan)
- Using `vsg::CopyImageToBuffer` (VSG command node)
- Submitting via `vsg::Fence` and the device queue

- [ ] **Step 3: Update CMakeLists to add new source files**

In `src/vsgocct/CMakeLists.txt`, add to VSGOCCT_SOURCES and VSGOCCT_HEADERS:

```cmake
set(VSGOCCT_SOURCES
    StepModelLoader.cpp
    cad/StepReader.cpp
    mesh/ShapeMesher.cpp
    scene/SceneBuilder.cpp
    pick/PickHandler.cpp
)

set(VSGOCCT_HEADERS
    ../../include/vsgocct/StepModelLoader.h
    ../../include/vsgocct/cad/StepReader.h
    ../../include/vsgocct/mesh/ShapeMesher.h
    ../../include/vsgocct/scene/SceneBuilder.h
    ../../include/vsgocct/pick/FaceIdCodec.h
    ../../include/vsgocct/pick/PickHandler.h
)
```

- [ ] **Step 4: Build to verify compilation**

Run: `cmake --build build`
Expected: Clean compilation.

- [ ] **Step 5: Commit**

```bash
git add include/vsgocct/pick/PickHandler.h src/vsgocct/pick/PickHandler.cpp src/vsgocct/CMakeLists.txt
git commit -m "feat(pick): add PickHandler for mouse event capture and pixel readback"
```

---

### Task 5: SelectionManager for face highlight

**Files:**
- Create: `include/vsgocct/pick/SelectionManager.h`
- Create: `src/vsgocct/pick/SelectionManager.cpp`
- Modify: `src/vsgocct/CMakeLists.txt`

- [ ] **Step 1: Create SelectionManager header**

Create `include/vsgocct/pick/SelectionManager.h`:

```cpp
#pragma once

#include <cstdint>
#include <vector>

#include <vsg/maths/vec3.h>
#include <vsg/core/ref_ptr.h>
#include <vsg/core/Array.h>

#include <vsgocct/scene/SceneBuilder.h>

namespace vsgocct::pick
{
class SelectionManager
{
public:
    void selectFace(uint32_t faceId, const scene::AssemblySceneData& sceneData);
    void clearSelection();
    uint32_t selectedFaceId() const { return _selectedFaceId; }

private:
    static constexpr vsg::vec3 HIGHLIGHT_COLOR{1.0f, 0.8f, 0.2f};

    uint32_t _selectedFaceId = 0;
    std::vector<vsg::vec3> _originalColors;
    vsg::ref_ptr<vsg::vec3Array> _activeColorArray;
    uint32_t _activeVertexOffset = 0;
    uint32_t _activeVertexCount = 0;
};
} // namespace vsgocct::pick
```

- [ ] **Step 2: Create SelectionManager implementation**

Create `src/vsgocct/pick/SelectionManager.cpp`:

```cpp
#include <vsgocct/pick/SelectionManager.h>

namespace vsgocct::pick
{
void SelectionManager::selectFace(uint32_t faceId, const scene::AssemblySceneData& sceneData)
{
    if (faceId == _selectedFaceId)
    {
        return;
    }

    clearSelection();

    if (faceId == 0)
    {
        return;
    }

    auto refIt = sceneData.faceColorRefs.find(faceId);
    if (refIt == sceneData.faceColorRefs.end() || !refIt->second.colorArray)
    {
        return;
    }

    const auto& colorRef = refIt->second;
    _activeColorArray = colorRef.colorArray;
    _activeVertexOffset = colorRef.vertexOffset;
    _activeVertexCount = colorRef.vertexCount;

    // Backup original colors and apply highlight
    _originalColors.resize(_activeVertexCount);
    for (uint32_t i = 0; i < _activeVertexCount; ++i)
    {
        _originalColors[i] = (*_activeColorArray)[_activeVertexOffset + i];
        (*_activeColorArray)[_activeVertexOffset + i] = HIGHLIGHT_COLOR;
    }
    _activeColorArray->dirty();

    _selectedFaceId = faceId;
}

void SelectionManager::clearSelection()
{
    if (_selectedFaceId == 0 || !_activeColorArray)
    {
        return;
    }

    for (uint32_t i = 0; i < _activeVertexCount; ++i)
    {
        (*_activeColorArray)[_activeVertexOffset + i] = _originalColors[i];
    }
    _activeColorArray->dirty();

    _selectedFaceId = 0;
    _originalColors.clear();
    _activeColorArray = {};
    _activeVertexOffset = 0;
    _activeVertexCount = 0;
}
} // namespace vsgocct::pick
```

**Note on `dirty()`**: After calling `dirty()`, VSG's `TransferTask` will re-upload the modified region to the GPU on the next frame. This works automatically if the data was created with default VSG allocations. If `dirty()` has no effect (colors don't change visually), the vertex data may need to be created with explicit `VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT`. Investigate during implementation if the highlight does not appear.

- [ ] **Step 3: Update CMakeLists**

Add `pick/SelectionManager.cpp` to VSGOCCT_SOURCES and `../../include/vsgocct/pick/SelectionManager.h` to VSGOCCT_HEADERS.

- [ ] **Step 4: Build to verify compilation**

Run: `cmake --build build`
Expected: Clean compilation.

- [ ] **Step 5: Commit**

```bash
git add include/vsgocct/pick/SelectionManager.h src/vsgocct/pick/SelectionManager.cpp src/vsgocct/CMakeLists.txt
git commit -m "feat(pick): add SelectionManager for face highlight via color buffer modification"
```

---

## Chunk 4: Viewer Integration

### Task 6: Wire picking into the Qt viewer

**Files:**
- Modify: `examples/vsgqt_step_viewer/main.cpp`

- [ ] **Step 1: Add offscreen framebuffer creation**

Add a helper function to `main.cpp` (in the anonymous namespace) that creates the offscreen resources needed for the pick render pass. This function creates:
- Color image (`VK_FORMAT_R8G8B8A8_UNORM`, `VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT`, `VK_SAMPLE_COUNT_1_BIT`)
- Depth image (`VK_FORMAT_D32_SFLOAT`, `VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT`)
- ImageViews for both
- RenderPass with one subpass (color attachment ref 0, depth attachment ref 1)
- Framebuffer wrapping the render pass and image views

**Consult VSG examples** for exact API — `vsg::RenderPass::create` requires `AttachmentDescription` arrays, `SubpassDescription` with attachment references, and optionally `SubpassDependency`.

Return the color image (for PickHandler) and the RenderGraph (for the CommandGraph).

- [ ] **Step 2: Update createRenderWindow to add pick CommandGraph**

In `createRenderWindow()`, after creating the main command graph, create the offscreen resources and a pick command graph:

```cpp
auto commandGraph = vsg::createCommandGraphForView(*window, camera, sceneData.scene);

// Create offscreen pick framebuffer
auto offscreen = createOffscreenFramebuffer(traits->device, width, height);

// Create pick RenderGraph targeting the offscreen framebuffer
auto pickRenderGraph = vsg::RenderGraph::create();
pickRenderGraph->framebuffer = offscreen.framebuffer;
pickRenderGraph->renderArea = {{0, 0}, {width, height}};
pickRenderGraph->clearValues = {
    {{0.0f, 0.0f, 0.0f, 0.0f}},  // color: black = no hit
    {{0.0f, 0}}                    // depth: 0 for reverse-Z
};
pickRenderGraph->addChild(vsg::View::create(camera, sceneData.pickScene));

auto pickCommandGraph = vsg::CommandGraph::create(*window);
pickCommandGraph->addChild(pickRenderGraph);

viewer->addRecordAndSubmitTaskAndPresentation({commandGraph, pickCommandGraph});
```

Return the offscreen color image alongside the window.

- [ ] **Step 3: Create PickHandler and SelectionManager, wire callbacks**

Add includes:
```cpp
#include <vsgocct/pick/PickHandler.h>
#include <vsgocct/pick/SelectionManager.h>
```

In `main()`, after creating the render window:

```cpp
auto selectionManager = std::make_shared<vsgocct::pick::SelectionManager>();

auto pickHandler = vsgocct::pick::PickHandler::create(
    offscreenColorImage, traits->device, sceneData);

pickHandler->setPickCallback(
    [selectionManager, &sceneData, statusBar = mainWindow.statusBar()](
        const vsgocct::scene::PickResult& result)
    {
        selectionManager->selectFace(result.faceId, sceneData);

        if (result.faceInfo)
        {
            statusBar->showMessage(
                QStringLiteral("Face %1 | Part: %2 | Normal: (%3, %4, %5)")
                    .arg(result.faceId)
                    .arg(QString::fromStdString(result.faceInfo->partName))
                    .arg(result.faceInfo->faceNormal.x, 0, 'f', 2)
                    .arg(result.faceInfo->faceNormal.y, 0, 'f', 2)
                    .arg(result.faceInfo->faceNormal.z, 0, 'f', 2));
        }
        else
        {
            selectionManager->clearSelection();
            statusBar->showMessage(QStringLiteral("Parts: %1 | Triangles: %2 | Lines: %3 | Points: %4")
                .arg(static_cast<qulonglong>(sceneData.parts.size()))
                .arg(static_cast<qulonglong>(sceneData.totalTriangleCount))
                .arg(static_cast<qulonglong>(sceneData.totalLineSegmentCount))
                .arg(static_cast<qulonglong>(sceneData.totalPointCount)));
        }
    });

viewer->addEventHandler(pickHandler);
```

- [ ] **Step 4: Add info panel widget to overlay**

Add a face info section to the overlay (after the parts list scroll area):

```cpp
auto* separator = new QFrame(panel);
separator->setFrameShape(QFrame::HLine);
separator->setStyleSheet("color: rgba(255,255,255,30);");
panelLayout->addWidget(separator);

auto* infoTitle = new QLabel(QStringLiteral("Selected Face"), panel);
infoTitle->setObjectName(QStringLiteral("partListTitle"));
auto* faceIdLabel = new QLabel(panel);
auto* partNameLabel = new QLabel(panel);
auto* normalLabel = new QLabel(panel);
faceIdLabel->setStyleSheet("color: rgba(255,255,255,180); font-size: 12px; padding: 2px 6px;");
partNameLabel->setStyleSheet("color: rgba(255,255,255,180); font-size: 12px; padding: 2px 6px;");
normalLabel->setStyleSheet("color: rgba(255,255,255,180); font-size: 12px; padding: 2px 6px;");

panelLayout->addWidget(infoTitle);
panelLayout->addWidget(faceIdLabel);
panelLayout->addWidget(partNameLabel);
panelLayout->addWidget(normalLabel);

// Initially hidden
infoTitle->setVisible(false);
separator->setVisible(false);
faceIdLabel->setVisible(false);
partNameLabel->setVisible(false);
normalLabel->setVisible(false);
```

Update the pick callback to show/hide the info labels:

```cpp
// Capture info labels in callback
pickHandler->setPickCallback(
    [=, &sceneData](const vsgocct::scene::PickResult& result)
    {
        selectionManager->selectFace(result.faceId, sceneData);
        bool hasFace = result.faceInfo != nullptr;

        infoTitle->setVisible(hasFace);
        separator->setVisible(hasFace);
        faceIdLabel->setVisible(hasFace);
        partNameLabel->setVisible(hasFace);
        normalLabel->setVisible(hasFace);

        if (hasFace)
        {
            faceIdLabel->setText(QStringLiteral("Face ID: %1").arg(result.faceId));
            partNameLabel->setText(QStringLiteral("Part: %1").arg(
                QString::fromStdString(result.faceInfo->partName)));
            normalLabel->setText(QStringLiteral("Normal: (%1, %2, %3)")
                .arg(result.faceInfo->faceNormal.x, 0, 'f', 2)
                .arg(result.faceInfo->faceNormal.y, 0, 'f', 2)
                .arg(result.faceInfo->faceNormal.z, 0, 'f', 2));
        }
    });
```

- [ ] **Step 5: Sync visibility toggles for pick switches**

Update the part checkbox toggle to also sync the pick switch:

```cpp
auto switchNode = part.switchNode;
auto pickSwitchNode = part.pickSwitchNode;
QObject::connect(checkbox, &QCheckBox::toggled, &mainWindow,
    [switchNode, pickSwitchNode](bool visible)
    {
        auto mask = visible ? vsg::MASK_ALL : vsg::MASK_OFF;
        if (switchNode && !switchNode->children.empty())
        {
            switchNode->children.front().mask = mask;
        }
        if (pickSwitchNode && !pickSwitchNode->children.empty())
        {
            pickSwitchNode->children.front().mask = mask;
        }
    });
```

- [ ] **Step 6: Build and manually test**

Run: `cmake --build build --target vsgqt_step_viewer`
Expected: Application compiles. Load a STEP file and verify:
- Model renders normally
- Left-click on a face → face turns yellow, info labels show
- Left-click on background → highlight clears, info labels hide
- Toggle part visibility → hidden parts are not pickable
- Rotate camera → picking works from different angles

- [ ] **Step 7: Commit**

```bash
git add examples/vsgqt_step_viewer/main.cpp
git commit -m "feat(viewer): wire face picking, highlight, and info panel into Qt viewer"
```

---

## Chunk 5: Final Verification

### Task 7: Run full test suite and integration test

- [ ] **Step 1: Build everything**

Run: `cmake --build build`
Expected: Clean compilation with no warnings.

- [ ] **Step 2: Run all tests**

Run: `ctest --test-dir build -V`
Expected: All tests PASS.

- [ ] **Step 3: Fix any failures**

If any tests fail, investigate and fix.

- [ ] **Step 4: Manual integration test**

Launch: `./build/examples/vsgqt_step_viewer/vsgqt_step_viewer path/to/model.step`

Verify:
1. Model loads and renders normally
2. Left-click on a face → face highlights yellow, info panel shows face ID, part name, normal
3. Left-click on background → highlight clears, info panel hides
4. Toggle part visibility → hidden parts are not pickable
5. Rotate camera → picking still works correctly from different angles
6. Click different faces in sequence → only one highlighted at a time

- [ ] **Step 5: Final commit if any fixes were needed**

```bash
git add -A
git commit -m "fix: address issues found during integration testing"
```
