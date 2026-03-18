# STL Reader Design

## Overview

Add STL model reading to vsgOcct with full picking support (face, edge, vertex) and a unified `ModelLoader` API that dispatches by file extension.

## Requirements

- Read both ASCII and Binary STL formats (auto-detect)
- Reconstruct edge and vertex topology from triangle mesh via feature-edge detection
- Full picking support: face, edge, vertex selection and hover highlighting
- Unified API: `vsgocct::loadScene("model.stl")` works alongside `loadScene("model.step")`
- Reuse existing `SceneBuilder` rendering pipeline

## Architecture

### Data Flow

```
STL file
  → cad::readStl()           // OCCT RWStl, returns Poly_Triangulation
  → mesh::buildStlMesh()     // Extract geometry + reconstruct edges/points → MeshResult
  → scene::buildPartScene()  // Build VSG scene node for one part
  → scene::assembleScene()   // Wrap into AssemblySceneData
```

### Comparison with STEP Pipeline

```
STEP: readStep() → AssemblyData → [for each part: triangulate() → MeshResult] → buildAssemblyScene()
STL:  readStl()  → StlData     → buildStlMesh() → MeshResult → buildPartScene() → assembleScene()
```

Both pipelines converge at `MeshResult` → `SceneBuilder`, sharing all rendering, selection, and material logic.

## New Modules

### 1. cad::StlReader

**Files**: `include/vsgocct/cad/StlReader.h`, `src/vsgocct/cad/StlReader.cpp`

```cpp
namespace vsgocct::cad
{
struct StlData
{
    Handle(Poly_Triangulation) triangulation;
    std::string name;  // From ASCII "solid <name>" or filename stem
};

StlData readStl(const std::filesystem::path& stlFile);
}
```

**Implementation**:
- Uses `RWStl::ReadFile()` which handles both ASCII and Binary formats automatically
- Extracts model name from ASCII STL header (`solid <name>`) or uses filename stem for Binary
- Throws `std::runtime_error` on read failure or empty triangulation

### 2. mesh::StlMeshBuilder

**Files**: `include/vsgocct/mesh/StlMeshBuilder.h`, `src/vsgocct/mesh/StlMeshBuilder.cpp`

```cpp
namespace vsgocct::mesh
{
struct StlMeshOptions
{
    double edgeAngleThreshold = 30.0;  // Degrees: dihedral angle threshold for feature edges
};

MeshResult buildStlMesh(const Handle(Poly_Triangulation)& triangulation,
                         const StlMeshOptions& options = {});
}
```

**Feature Edge Reconstruction Algorithm**:

1. **Build half-edge map**: For each triangle, create entries for its three edges keyed by `(min(v0,v1), max(v0,v1))`. Each entry stores the two adjacent face indices.

2. **Classify edges**:
   - **Boundary edges**: Referenced by only one triangle → always feature edges
   - **Sharp edges**: Dihedral angle between two adjacent face normals exceeds `edgeAngleThreshold` → feature edges
   - **Smooth edges**: Below threshold → discarded (not rendered)

3. **Extract feature vertices**: Endpoints of all feature edges become feature vertices (deduplicated).

4. **Build MeshResult**:
   - `facePositions` / `faceNormals`: All triangles, one `FaceSpan` per triangle (faceId = triangle index)
   - `linePositions`: Feature edge segments, one `LineSpan` per feature edge (edgeId = sequential)
   - `pointPositions`: Feature vertices, one `PointSpan` per vertex (vertexId = sequential)
   - `boundsMin` / `boundsMax`: Computed during face extraction

**Why per-triangle FaceSpan**: STL has no higher-level face grouping. Each triangle is independently selectable, consistent with the data format. For large models this means many spans, but the overhead is manageable since spans are lightweight (12 bytes each).

### 3. SceneBuilder Refactoring

**Existing function** `buildAssemblyScene` is refactored to expose two reusable building blocks:

```cpp
namespace vsgocct::scene
{
// Build a single part's scene node from mesh data + metadata
PartSceneNode buildPartScene(
    uint32_t partId,
    const std::string& name,
    const mesh::MeshResult& mesh,
    const cad::ShapeNodeColor& color,
    const cad::ShapeVisualMaterial& material,
    const SceneOptions& sceneOptions);

// Assemble multiple PartSceneNodes into final scene
AssemblySceneData assembleScene(
    std::vector<PartSceneNode>&& parts,
    const SceneOptions& sceneOptions);
}
```

**Refactoring approach**: Extract the per-part rendering logic (pipeline creation, shader setup, buffer allocation, switch node construction) from the existing `buildAssemblyScene` into `buildPartScene`. The existing `buildAssemblyScene` calls these new functions internally, preserving backward compatibility.

### 4. Unified ModelLoader

**Files**: `include/vsgocct/ModelLoader.h`, `src/vsgocct/ModelLoader.cpp`

```cpp
namespace vsgocct
{
// Universal entry point - dispatches by file extension
scene::AssemblySceneData loadScene(
    const std::filesystem::path& modelFile,
    const mesh::MeshOptions& meshOptions = {},
    const scene::SceneOptions& sceneOptions = {});

// Explicit STL entry point
scene::AssemblySceneData loadStlScene(
    const std::filesystem::path& stlFile,
    const mesh::StlMeshOptions& stlOptions = {},
    const scene::SceneOptions& sceneOptions = {});
}
```

**Extension mapping**:
- `.step`, `.stp` (case-insensitive) → `loadStepScene()`
- `.stl` (case-insensitive) → `loadStlScene()`
- Unknown extension → `std::runtime_error`

**Note**: `loadStepScene()` remains unchanged and continues to work. `StepModelLoader.h` is preserved for backward compatibility but `ModelLoader.h` becomes the preferred API.

## Build Changes

**CMake** (`src/vsgocct/CMakeLists.txt`):
- Add new source files: `cad/StlReader.cpp`, `mesh/StlMeshBuilder.cpp`, `ModelLoader.cpp`
- Add OCCT dependency: `TKSTL` (contains `RWStl`)

## Testing

### test_stl_reader.cpp
- Read ASCII STL file → verify triangle count and name extraction
- Read Binary STL file → verify triangle count
- Invalid file path → verify exception
- Empty STL → verify exception

### test_stl_mesh_builder.cpp
- Cube STL (8 vertices, 12 triangles) → verify 12 FaceSpans, 12 feature edges (all 90-degree dihedral), 8 feature vertices
- Sphere-like STL (smooth surface) → verify few/no feature edges with default threshold
- Single triangle → verify 1 FaceSpan, 3 boundary edges, 3 vertices

### Test Data Generation
Extend `generate_test_data.cpp` to produce:
- `cube.stl` (ASCII format, 12 triangles)
- `cube_binary.stl` (Binary format, same geometry)

## STL Limitations (Documented)

Unlike STEP models, STL models have these inherent limitations:
- **No assembly hierarchy**: Always loaded as a single part (partId = 0)
- **No native colors/materials**: Uses default material; user can apply presets post-load
- **No B-Rep topology**: Edges and vertices are reconstructed geometrically and may not match CAD intent
- **Per-triangle selection**: Each triangle is an independent selectable face (no face grouping)

## File Summary

| File | Action |
|------|--------|
| `include/vsgocct/cad/StlReader.h` | New |
| `src/vsgocct/cad/StlReader.cpp` | New |
| `include/vsgocct/mesh/StlMeshBuilder.h` | New |
| `src/vsgocct/mesh/StlMeshBuilder.cpp` | New |
| `include/vsgocct/ModelLoader.h` | New |
| `src/vsgocct/ModelLoader.cpp` | New |
| `src/vsgocct/scene/SceneBuilder.cpp` | Refactor: extract buildPartScene + assembleScene |
| `include/vsgocct/scene/SceneBuilder.h` | Add buildPartScene + assembleScene declarations |
| `src/vsgocct/CMakeLists.txt` | Add new sources + TKSTL dependency |
| `tests/test_stl_reader.cpp` | New |
| `tests/test_stl_mesh_builder.cpp` | New |
| `tests/generate_test_data.cpp` | Extend with STL generation |
| `tests/CMakeLists.txt` | Add new test targets |
