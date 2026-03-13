# M1a: Assembly Tree Preservation — Design Spec

## Overview

Upgrade `vsgocct::cad` from `STEPControl_Reader` (flat shape) to `STEPCAFControl_Reader` (XCAF), producing a `ShapeNode` tree that preserves assembly hierarchy, part names, and colors. Upgrade `vsgocct::scene` to render per-part VSG subgraphs with individual visibility control and per-part colors.

## Scope

**In scope:**
- XCAF-based STEP reading with assembly tree extraction
- `ShapeNode` tree data structure (name, color, location, children)
- Per-part VSG subgraph with `vsg::Switch` for visibility
- Per-part color via push constant
- Replacement of existing `ShapeData` / `StepSceneData` APIs
- Test data generation for colored and nested assemblies
- Viewer update to use part list instead of point/line/face toggles

**Out of scope:**
- ShapeId / FaceId stable ID system
- Picking / highlighting / isolation
- Bidirectional mapping (VSG node ↔ OCCT shape)
- Per-face grouping
- Cache format / serialization

## Data Structures

### ShapeNode (cad module output)

```cpp
namespace vsgocct::cad
{
enum class ShapeNodeType
{
    Assembly,    // Has children, no geometry
    Part         // Leaf node, holds actual TopoDS_Shape
};

struct ShapeNodeColor
{
    float r = 0.74f, g = 0.79f, b = 0.86f;  // Default gray-blue
    bool isSet = false;                       // Whether color was read from XCAF
};

struct ShapeNode
{
    ShapeNodeType type = ShapeNodeType::Part;
    std::string name;                          // From TDataStd_Name
    ShapeNodeColor color;                      // From XCAFDoc_ColorTool (surface color)
    TopoDS_Shape shape;                        // Part nodes hold geometry; Assembly is empty
    TopLoc_Location location;                  // Transform relative to parent
    std::vector<ShapeNode> children;           // Children for Assembly/Reference nodes
};
}
```

Design decisions:
- Only two node types: `Assembly` (has children) and `Part` (has shape). XCAF's "Reference" concept is an implementation detail — references are resolved during tree construction, not exposed in the public API.
- Value semantics (`std::vector<ShapeNode>`) — tree owned recursively. For large assemblies (1000+ parts) this may need optimization (e.g., flat vector with parent indices); acceptable for M1a scope.
- `TopLoc_Location` retained because mesh module needs it for triangle transforms. Stores the **relative** transform from parent; the scene builder composes absolute locations during traversal.
- Color reads `XCAFDoc_ColorSurf` only (most common in STEP)
- No ShapeId — deferred to M1b

### AssemblyData (readStep return value)

```cpp
struct AssemblyData
{
    std::vector<ShapeNode> roots;  // Top-level free shapes (typically 1 root assembly)
};
```

Replaces the current `ShapeData` struct.

### AssemblySceneData (scene module output)

```cpp
namespace vsgocct::scene
{
struct PartSceneNode
{
    std::string name;
    vsg::ref_ptr<vsg::Switch> switchNode;  // Per-part visibility control
};

struct AssemblySceneData
{
    vsg::ref_ptr<vsg::Node> scene;         // VSG root node
    std::vector<PartSceneNode> parts;      // All parts for UI control

    vsg::dvec3 center;
    double radius = 1.0;

    std::size_t totalTriangleCount = 0;
    std::size_t totalLineSegmentCount = 0;
    std::size_t totalPointCount = 0;
};
}
```

Replaces the current `StepSceneData` struct. The global point/line/face Switch nodes are removed; each part has its own Switch.

## cad Module: readStep() Implementation

### API Change

```cpp
// Replaces: ShapeData readStep(path, options)
AssemblyData readStep(const std::filesystem::path& stepFile,
                      const ReaderOptions& options = {});
```

### Internal Flow

```
readStep(path):
  1. STEPCAFControl_Reader reader
  2. Open file as std::ifstream, call reader.ReadStream(name, stream)
     — Use ReadStream (not ReadFile) for Unicode path safety,
       consistent with current STEPControl_Reader implementation
  3. Handle(TDocStd_Document) doc = new TDocStd_Document("XDE")
  4. reader.Transfer(doc)
  5. shapeTool = XCAFDoc_DocumentTool::ShapeTool(doc->Main())
  6. colorTool = XCAFDoc_DocumentTool::ColorTool(doc->Main())
  7. TDF_LabelSequence freeLabels
  8. shapeTool->GetFreeShapes(freeLabels)
  9. For each freeLabel: roots.push_back(buildShapeNode(label, shapeTool, colorTool))
  10. Return AssemblyData{roots}
      — TDocStd_Document released after return
```

### buildShapeNode() Recursion

XCAF assembly structure: `GetFreeShapes` returns top-level labels. Each label may be an assembly, a reference (component), or a simple shape. Components obtained via `GetComponents()` are references — they carry a `TopLoc_Location` and point to a prototype label via `GetReferredShape()`. The prototype may itself be an assembly (nested) or a simple shape (leaf).

The traversal resolves references transparently — the `Reference` concept is not exposed in the output `ShapeNode` tree. A reference is resolved by extracting its location and recursing into its referred prototype label.

```
buildShapeNode(label, shapeTool, colorTool, parentColor):
  node.name = read TDataStd_Name from label (empty string if absent)
  node.color = read XCAFDoc_ColorSurf from colorTool

  — Color inheritance: if color not set on this label, inherit parentColor
  if !node.color.isSet and parentColor.isSet:
    node.color = parentColor

  — Resolve references: if this label is a reference (component),
  — extract its location and recurse into the referred prototype
  resolvedLabel = label
  if shapeTool->IsReference(label):
    node.location = IsComponent? label's XCAFDoc_Location : identity
    GetReferredShape(label, refLabel)
    resolvedLabel = refLabel
    — re-read name from refLabel if node.name is empty
    — re-read color from refLabel if node.color is not set

  — Now process the resolved label (prototype)
  if shapeTool->IsAssembly(resolvedLabel):
    node.type = Assembly
    GetComponents(resolvedLabel, components)
    for each comp in components:
      node.children.push_back(buildShapeNode(comp, shapeTool, colorTool, node.color))

  else (IsSimpleShape):
    node.type = Part
    node.shape = shapeTool->GetShape(resolvedLabel)

  return node
```

### Error Handling

- File-level errors (file not found, invalid STEP format): throw `std::runtime_error`, same as current behavior.
- Part-level errors (null shape, tessellation failure for one part): **skip the bad part** and continue building the rest of the tree. Log a warning to stderr. This is more practical for real-world STEP files which often contain minor issues.
- If the entire document has zero valid parts after traversal: throw `std::runtime_error`.

### OCCT Dependency Changes

New link targets in `src/vsgocct/CMakeLists.txt`:
- `TKXDESTEP` — STEPCAFControl_Reader
- `TKLCAF` — TDocStd_Document, OCAF framework
- `TKXCAF` — XCAFDoc_ShapeTool, XCAFDoc_ColorTool

`TKDESTEP` can be kept or dropped (TKXDESTEP depends on it internally).

## mesh Module: No API Change

`mesh::triangulate(const TopoDS_Shape&, const MeshOptions&)` signature unchanged.

Callers apply location before calling:

```cpp
TopoDS_Shape locatedShape = node.shape.Located(absoluteLocation);
mesh::triangulate(locatedShape, meshOptions);
```

## scene Module: buildAssemblyScene()

### API

```cpp
AssemblySceneData buildAssemblyScene(
    const cad::AssemblyData& assembly,
    const mesh::MeshOptions& meshOptions = {},
    const SceneOptions& sceneOptions = {});
```

Replaces `buildScene(const mesh::MeshResult&, const SceneOptions&)`.

`SceneOptions` is simplified — the per-primitive-type visibility flags (`pointsVisible`, `linesVisible`, `facesVisible`) are removed. Visibility is now per-part via `PartSceneNode.switchNode`. If per-primitive-type global toggles are needed in the future, they can be added as a separate mechanism in M2.

```cpp
struct SceneOptions
{
    // Reserved for future options (e.g., default visibility)
};
```

### Internal Flow

```
buildAssemblyScene(assembly):
  root = vsg::Group::create()
  parts = []
  boundsAccum = {}

  for each rootNode in assembly.roots:
    buildNodeSubgraph(rootNode, root, identityLocation, parts, boundsAccum)

  compute center/radius from boundsAccum
  compute total counts from parts

  return AssemblySceneData{root, parts, center, radius, counts}

buildNodeSubgraph(shapeNode, parentGroup, accumulatedLocation):
  currentLocation = accumulatedLocation * shapeNode.location

  if Assembly or Reference:
    group = vsg::Group::create()
    for each child in shapeNode.children:
      buildNodeSubgraph(child, group, currentLocation)
    parentGroup->addChild(group)

  if Part:
    absoluteShape = shapeNode.shape.Located(currentLocation)
    meshResult = mesh::triangulate(absoluteShape)
    faceNode = createFaceNode(meshResult, resolveColor(shapeNode))
    lineNode = createLineNode(meshResult)
    pointNode = createPointNode(meshResult)
    partGroup = vsg::Group with face/line/point children
    partSwitch = vsg::Switch wrapping partGroup
    parentGroup->addChild(partSwitch)
    parts.push_back({shapeNode.name, partSwitch})
    update boundsAccum
```

### Per-Part Color via Push Constant

Face shaders push constants extended. The `baseColor` is passed from vertex to fragment as a varying (not accessed directly in fragment push constants) to keep the push constant range within vertex stage only:

```glsl
// Vertex shader
layout(push_constant) uniform PushConstants {
    mat4 projection;
    mat4 modelView;
    vec4 baseColor;  // NEW: per-part color (rgb + alpha)
};

layout(location = 0) out vec3 viewNormal;
layout(location = 1) out vec3 partColor;  // NEW: pass to fragment

void main() {
    // ... existing transform code ...
    partColor = baseColor.rgb;
}
```

```glsl
// Fragment shader
layout(location = 0) in vec3 viewNormal;
layout(location = 1) in vec3 partColor;  // NEW: from vertex

void main() {
    // ... existing normal/lighting code ...
    vec3 shadedColor = partColor * (0.24 + 0.76 * diffuse);
    fragmentColor = vec4(shadedColor, 1.0);
}
```

Push constant range grows from 128 to 144 bytes. The Vulkan guaranteed minimum `maxPushConstantsSize` is 128 bytes, but all desktop GPUs support at least 256 bytes. Push constant stage flags remain `VK_SHADER_STAGE_VERTEX_BIT` only since color is passed as a varying. This spec targets desktop Vulkan only; mobile/embedded platforms are not supported.

Color resolution: if `shapeNode.color.isSet`, use it; otherwise use default gray-blue `(0.74, 0.79, 0.86)`.

## Facade Change

```cpp
// StepModelLoader.h
namespace vsgocct
{
scene::AssemblySceneData loadStepScene(const std::filesystem::path& path);
}

// Implementation:
//   auto assembly = cad::readStep(path);
//   return scene::buildAssemblyScene(assembly);
```

## Example Viewer Update

- Replace `StepSceneData` with `AssemblySceneData`
- Floating overlay: replace Points/Lines/Faces toggle buttons with a **part list**, each part has a visibility toggle showing `part.name`
- Status bar: use `totalTriangleCount`, `totalLineSegmentCount`, `totalPointCount`
- Camera initialization: `center` / `radius` unchanged

## Test Plan

### Test Data Generation (extend generate_test_data.cpp)

New STEP files using XDE document + XCAFDoc_ColorTool:
- `colored_box.step` — single box with red surface color
- `nested_assembly.step` — Assembly → SubAssembly → 2 Parts (box + cylinder), with distinct colors and names

### cad Module Tests (extend test_step_reader.cpp)

1. Single part STEP → roots has 1 Part node, name is non-empty
2. Assembly STEP → root node type is Assembly, children contain Parts
3. Colored STEP → Part node color.isSet == true, RGB values correct
4. Nested assembly → multi-level tree structure correct
5. Assembly with location → child Part location is not identity
6. Plain STEP (non-XCAF, e.g., existing box.step) → still produces valid tree (single Part node, backward compatible)
7. Shared instances (same prototype used twice with different locations) → produces two separate Part subtrees with distinct locations

### scene Module Tests (extend test_scene_builder.cpp)

8. buildAssemblyScene basic → scene is non-null, parts is non-empty
9. parts list count matches leaf Part node count
10. Each part switchNode toggles visibility
11. Colored part → vertex shader receives correct color values

## File Changes Summary

| File | Action |
|------|--------|
| `include/vsgocct/cad/StepReader.h` | Replace ShapeData with ShapeNode + AssemblyData |
| `src/vsgocct/cad/StepReader.cpp` | Rewrite: STEPCAFControl_Reader + recursive tree builder |
| `include/vsgocct/scene/SceneBuilder.h` | Replace buildScene with buildAssemblyScene, add AssemblySceneData |
| `src/vsgocct/scene/SceneBuilder.cpp` | Rewrite: recursive subgraph builder, per-part color push constant |
| `include/vsgocct/StepSceneData.h` | Delete (replaced by AssemblySceneData in SceneBuilder.h) |
| `include/vsgocct/StepModelLoader.h` | Update return type to AssemblySceneData |
| `src/vsgocct/StepModelLoader.cpp` | Update to call readStep → buildAssemblyScene |
| `src/vsgocct/CMakeLists.txt` | Add TKXDESTEP, TKLCAF, TKXCAF link targets |
| `examples/vsgqt_step_viewer/main.cpp` | Update to AssemblySceneData, part list UI |
| `tests/generate_test_data.cpp` | Add colored_box.step, nested_assembly.step |
| `tests/test_step_reader.cpp` | Add 5 assembly tree tests |
| `tests/test_scene_builder.cpp` | Add 4 assembly scene tests |

## References

- [OCCT XDE User Guide](https://dev.opencascade.org/doc/overview/html/occt_user_guides__xde.html)
- [XCAFDoc_ShapeTool Reference](https://dev.opencascade.org/doc/occt-6.9.1/refman/html/class_x_c_a_f_doc___shape_tool.html)
- [STEPCAFControl_Reader Reference](https://dev.opencascade.org/doc/refman/html/class_s_t_e_p_c_a_f_control___reader.html)
- [OpenSceneGraph OCCT Plugin](https://github.com/openscenegraph/OpenSceneGraph/blob/master/src/osgPlugins/OpenCASCADE/ReaderWriterOpenCASCADE.cpp) — reference for XCAF traversal pattern
