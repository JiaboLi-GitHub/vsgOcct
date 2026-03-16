# Face-Level Color-Coded Picking Design

## Summary

Implement face-level picking for vsgOcct using color-coded offscreen rendering. Each `TopoDS_Face` is assigned a unique global ID, encoded as an RGB color, and rendered to an offscreen framebuffer every frame. On mouse click, the pixel color at the click position is read back and decoded to identify the picked face. The system provides visual highlight feedback and an information panel showing face properties.

## Technical Approach

**Color-coded picking** (also known as "color picking" or "ID buffer"):

1. Assign a unique `uint32_t faceId` to each `TopoDS_Face` during meshing
2. Encode `faceId` into RGB channels and render to an offscreen color attachment every frame
3. On click, read the pixel at the click position, decode to `faceId`, and look up face metadata

## ID Encoding Scheme

- **Type**: `uint32_t`, starting from **1** (0 reserved for background/no-hit)
- **Encoding**: `R = (faceId >> 16) & 0xFF, G = (faceId >> 8) & 0xFF, B = faceId & 0xFF`
- **Capacity**: 24-bit = ~16.7 million faces (far exceeds practical needs)
- **Background**: clear color `(0, 0, 0, 0)` → faceId=0 → no hit

## Data Model Changes

### mesh module — MeshResult additions

```cpp
struct FaceData {
    uint32_t faceId;           // Global unique ID (starts from 1)
    uint32_t triangleOffset;   // Start index in facePositions (triangle index, not vertex)
    uint32_t triangleCount;    // Number of triangles belonging to this Face
    vsg::vec3 normal;          // Area-weighted average of triangle normals for this face
    double area;               // Face area (computed lazily via BRepGProp on first access, or cached during pick)
};

// Added to MeshResult:
std::vector<FaceData> faces;              // All face metadata
std::vector<uint32_t> perTriangleFaceId;  // faceId for each triangle (parallel to triangles)
```

**ID assignment**: The `triangulate()` function signature gains a `uint32_t& nextFaceId` parameter:

```cpp
MeshResult triangulate(const TopoDS_Shape& shape, uint32_t& nextFaceId,
                       const MeshOptions& options = {});
```

The caller (scene builder) maintains the global counter and passes it to each `triangulate()` call. This keeps `triangulate()` stateless and thread-safe. The counter starts at 1 (0 is reserved for background).

### scene module — New picking structures

```cpp
struct FaceInfo {
    uint32_t faceId;
    std::string partName;      // Name of the owning Part
    uint32_t partIndex;        // Index in AssemblySceneData::parts
    vsg::vec3 faceNormal;      // Area-weighted average face normal
    double faceArea;           // Face area
};

struct PickResult {
    uint32_t faceId;           // Picked face ID (0 = no hit)
    const FaceInfo* faceInfo;  // Pointer to face metadata (nullptr if no hit)
};

// Added to AssemblySceneData:
std::unordered_map<uint32_t, FaceInfo> faceRegistry;  // faceId → metadata
```

### Highlight support data

To support highlight, the scene builder must store per-part references to the color array and the face-to-offset mapping:

```cpp
struct PartColorRef {
    vsg::ref_ptr<vsg::vec3Array> colorArray;  // Per-vertex color array for this part's faces
    uint32_t vertexOffset;                     // Offset into color array for this part
};

// Added to AssemblySceneData:
std::unordered_map<uint32_t, PartColorRef> faceColorRefs;  // faceId → color array + offset
```

## ID Color Buffer Creation

For the pick pipeline, each part needs a per-vertex ID color buffer (parallel to the position buffer). Created during scene building:

1. For each Part, iterate `MeshResult.perTriangleFaceId`
2. For each triangle, encode its `faceId` as `vec3(R/255.0, G/255.0, B/255.0)`
3. Replicate the encoded color for all 3 vertices of the triangle
4. Create a `vsg::vec3Array` with these encoded ID colors

The pick pipeline's `VertexIndexDraw` shares the same position array and index array as the main pipeline, but uses the ID color array instead of the visual color array. This means separate `VertexIndexDraw` instances per part — one for main render, one for pick render — sharing position/index data but differing in the color binding.

## Offscreen Rendering Pipeline

### Framebuffer Setup Sequence

The offscreen framebuffer is created manually (not via `createCommandGraphForView` which targets a window):

```
1. Create vsg::Image for color attachment (VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT)
2. Create vsg::ImageView for the color image
3. Create vsg::Image for depth attachment (VK_FORMAT_D32_SFLOAT, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT)
4. Create vsg::ImageView for the depth image
5. Create vsg::RenderPass with color and depth attachment descriptions (no MSAA, load op = CLEAR, store op = STORE)
6. Create vsg::Framebuffer from the render pass and image views
7. Create vsg::RenderGraph with the custom framebuffer and render pass
8. Build pick CommandGraph containing the RenderGraph
```

The pick `RenderGraph` must reference the **same `vsg::Camera`** as the main view to ensure pixel-coordinate correspondence between the screen and the offscreen buffer.

### Framebuffer Configuration

- **Color Attachment**: `VK_FORMAT_R8G8B8A8_UNORM`, `VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT`
  - **No MSAA** — multisampling would blend colors and corrupt IDs
  - Size matches main window; recreated on window resize
- **Depth Attachment**: `VK_FORMAT_D32_SFLOAT`, `VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT`
- **Clear color**: `(0, 0, 0, 0)`

### Window Resize Handling

On `vsg::ConfigureWindowEvent`, the offscreen framebuffer must be recreated:

1. Destroy old color/depth images, image views, framebuffer
2. Create new ones matching the new window dimensions
3. Update the pick RenderGraph's framebuffer reference
4. This is deferred to the next frame to avoid in-flight resource conflicts

### ID Shaders

**Vertex Shader** (`pick_vert.glsl`):
```glsl
layout(push_constant) uniform PushConstants {
    mat4 projection;
    mat4 modelView;
};
layout(location = 0) in vec3 vertex;
layout(location = 1) in vec3 idColor;  // Pre-encoded face ID color

layout(location = 0) out vec3 fragIdColor;

void main() {
    gl_Position = projection * modelView * vec4(vertex, 1.0);
    fragIdColor = idColor;
}
```

**Fragment Shader** (`pick_frag.glsl`):
```glsl
layout(location = 0) in vec3 fragIdColor;
layout(location = 0) out vec4 outColor;

void main() {
    outColor = vec4(fragIdColor, 1.0);  // Flat output, no lighting
}
```

### Pipeline Characteristics

- **No lighting, no blending** — flat color output to preserve ID precision
- **Only triangle faces rendered** — lines and points do not participate in picking
- **No backface culling** (`VK_CULL_MODE_NONE`) — consistent with main render pipeline which also uses no culling
- **Depth test enabled** — only the nearest face is picked

### Visibility Synchronization

The pick scene graph **shares the same `vsg::Switch` nodes** as the main scene graph. Both CommandGraphs traverse the same scene tree. When a part's Switch mask is set to `MASK_OFF`, both the main and pick renders skip it. This is achieved by having the pick RenderGraph's scene root point to the same `vsg::Group` root that the main render uses — just with a different StateGroup binding (pick pipeline vs. main pipeline).

### Integration with Render Loop

Two `CommandGraph` instances registered with the `vsg::Viewer`:

1. **Main CommandGraph** (existing) — normal visual rendering to swapchain
2. **Pick CommandGraph** (new) — offscreen ID buffer rendering to custom framebuffer

Both share the same camera and the same scene graph Switch nodes. The pick CommandGraph is added via `viewer->assignRecordAndSubmitTaskAndPresentation({mainCommandGraph, pickCommandGraph})`.

## Pixel Readback and Pick Logic

### Mouse Event Capture

A custom event handler `PickHandler` (inheriting `vsg::Inherit<vsg::Visitor, PickHandler>`):
- Listens for `vsg::ButtonPressEvent` (left mouse button)
- Records click coordinates `(x, y)` in window pixel space
- **Must be registered** via `viewer->addEventHandler(pickHandler)` alongside the existing `Trackball` and `CloseHandler`

### Readback Flow

1. **On click**: Record pixel coordinates `(x, y)` and set a pending-pick flag
2. **After pick CommandGraph completes for current frame**: Issue a one-shot transfer command
3. **Transfer**: Copy the 1x1 pixel region at `(x, y)` from the offscreen color image to a host-visible staging buffer using `vkCmdCopyImageToBuffer`
4. **Synchronize**: Submit the transfer command with a fence, wait for fence completion
5. **Decode**: Read RGBA from staging buffer → `faceId = (R << 16) | (G << 8) | B`
6. **Lookup**: Find `faceId` in `faceRegistry` to get `FaceInfo`
7. **Callback**: Notify application via callback with `PickResult`

### Readback Implementation

```cpp
// Setup (once):
// - Create a vsg::Buffer (4 bytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT, host-visible memory)
// - Create a vsg::Fence for synchronization

// Per-pick readback:
// 1. Record a command buffer with:
//    - Pipeline barrier: color attachment → transfer src
//    - vkCmdCopyImageToBuffer: copy 1x1 region at (x,y) to staging buffer
//    - Pipeline barrier: transfer src → color attachment
// 2. Submit command buffer with fence
// 3. Wait on fence
// 4. Map staging buffer, read 4 bytes (R, G, B, A)
// 5. Decode faceId
```

The readback happens synchronously after the frame render completes. Since this only occurs on mouse click (not every frame), the brief GPU stall is acceptable.

### Pick API

```cpp
using PickCallback = std::function<void(const PickResult& result)>;

class PickHandler : public vsg::Inherit<vsg::Visitor, PickHandler> {
public:
    void setPickCallback(PickCallback callback);
    void apply(vsg::ButtonPressEvent& event) override;

private:
    PickCallback _callback;
    vsg::ref_ptr<vsg::Image> _idImage;       // Offscreen color attachment
    vsg::ref_ptr<vsg::Buffer> _stagingBuffer; // Host-visible readback buffer
    vsg::ref_ptr<vsg::Fence> _fence;          // Synchronization fence
    vsg::ref_ptr<vsg::Device> _device;        // Vulkan device reference
    bool _pendingPick = false;
    int32_t _pickX = 0, _pickY = 0;
};
```

## Highlight and Information Panel

### Highlight Implementation

On face selection, modify the per-vertex color buffer of the selected face's triangles:

- **Highlight color**: Bright yellow `(1.0, 0.8, 0.2)`
- Use `FaceData.triangleOffset` and `triangleCount` to locate the exact vertex range
- Backup original colors before overwriting; restore on deselection

**Vertex data requirements**: Color arrays must be created with `VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT` to allow CPU-side updates after scene compilation. After modifying colors, call `colorArray->dirty()` to trigger GPU re-upload on the next frame.

**Locating the color array**: The `faceColorRefs` map in `AssemblySceneData` provides direct access from `faceId` to the relevant color array and vertex offset, avoiding scene graph traversal.

```cpp
class SelectionManager {
public:
    void selectFace(uint32_t faceId, const AssemblySceneData& sceneData);
    void clearSelection();
    const PickResult* getSelection() const;

private:
    uint32_t _selectedFaceId = 0;
    std::vector<vsg::vec3> _originalColors;  // Backup of overwritten colors
    vsg::ref_ptr<vsg::vec3Array> _activeColorArray;  // Currently modified array
    uint32_t _activeVertexOffset = 0;
    uint32_t _activeVertexCount = 0;
};
```

### Information Panel (Qt Side)

A floating Qt widget in the overlay area, displaying selected face properties:

```
+---------------------------+
| Selected Face             |
| ------------------------- |
| Face ID:    42            |
| Part:       Bolt_M10      |
| Normal:     (0, 0, 1)    |
| Area:       12.34 mm2    |
+---------------------------+
```

- Hidden when `faceId == 0` (no selection)
- Positioned in the overlay layout, non-overlapping with main view

## Module Boundaries

| Module | Responsibility | File Location |
|--------|---------------|---------------|
| `mesh` | Face ID assignment, `FaceData` population, `perTriangleFaceId` array | `include/vsgocct/mesh/ShapeMesher.h`, `src/vsgocct/mesh/ShapeMesher.cpp` |
| `scene` | ID color buffer creation, offscreen pipeline setup, `faceRegistry` population | `include/vsgocct/scene/SceneBuilder.h`, `src/vsgocct/scene/SceneBuilder.cpp` |
| `pick` (new) | `PickHandler` event handler, pixel readback, ID decoding | `include/vsgocct/pick/PickHandler.h`, `src/vsgocct/pick/PickHandler.cpp` |
| `selection` (new) | `SelectionManager` for highlight state and color buffer manipulation | `include/vsgocct/pick/SelectionManager.h`, `src/vsgocct/pick/SelectionManager.cpp` |
| Qt viewer | Information panel widget, wiring pick callback to UI | `examples/vsgqt_step_viewer/main.cpp` |

## Key Design Decisions

1. **Global face ID (not per-part)**: Simpler encoding, single lookup table, no ambiguity
2. **Per-frame offscreen render (not on-demand)**: Eliminates click-time latency; cost is one extra draw pass. For typical CAD assemblies (10k-500k triangles), this doubles vertex processing but remains well within modern GPU budgets at interactive frame rates.
3. **RGB encoding (not R32_UINT)**: Standard format, well-supported by VSG, visually debuggable
4. **Vertex color modification for highlight (not separate overlay geometry)**: Reuses existing data, no extra draw calls. Requires host-visible vertex buffers and `dirty()` calls.
5. **No MSAA on pick buffer**: Essential for ID precision — multisampling blends colors between faces
6. **`nextFaceId` parameter (not global/static counter)**: Keeps `triangulate()` stateless and thread-safe; caller owns the counter.
7. **Shared Switch nodes between main and pick scene graphs**: Automatic visibility synchronization without extra bookkeeping.
8. **Synchronous single-pixel readback on click**: Acceptable stall since it only happens on user interaction, not every frame.
