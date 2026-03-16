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
    vsg::vec3 normal;          // Average face normal
    double area;               // Face area (from OCCT BRepGProp)
};

// Added to MeshResult:
std::vector<FaceData> faces;              // All face metadata
std::vector<uint32_t> perTriangleFaceId;  // faceId for each triangle (parallel to triangles)
```

**ID assignment**: During `mesh::triangulate()`, as each `TopoDS_Face` is iterated, a global counter increments to assign `faceId`. The counter spans across all Parts in the assembly.

### scene module — New picking structures

```cpp
struct FaceInfo {
    uint32_t faceId;
    std::string partName;      // Name of the owning Part
    uint32_t partIndex;        // Index in AssemblySceneData::parts
    vsg::vec3 faceNormal;      // Average face normal
    double faceArea;           // Face area
};

struct PickResult {
    uint32_t faceId;           // Picked face ID (0 = no hit)
    const FaceInfo* faceInfo;  // Pointer to face metadata (nullptr if no hit)
};

// Added to AssemblySceneData:
std::unordered_map<uint32_t, FaceInfo> faceRegistry;  // faceId → metadata
```

## Offscreen Rendering Pipeline

### Framebuffer Configuration

- **Color Attachment**: `VK_FORMAT_R8G8B8A8_UNORM`, stores encoded face ID colors
  - **No MSAA** — multisampling would blend colors and corrupt IDs
  - Size matches main window; resized on window resize
- **Depth Attachment**: `VK_FORMAT_D32_SFLOAT`, ensures correct occlusion
- **Clear color**: `(0, 0, 0, 0)`

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
- **Backface culling** — consistent with main render pipeline
- **Depth test enabled** — only the nearest face is picked

### Integration with Render Loop

Two `CommandGraph` instances registered with the `vsg::Viewer`:

1. **Main CommandGraph** (existing) — normal visual rendering
2. **Pick CommandGraph** (new) — offscreen ID buffer rendering

Both share the same geometry data but use different graphics pipelines (different shaders). The pick command graph renders to an offscreen `vsg::Framebuffer` instead of the swapchain.

## Pixel Readback and Pick Logic

### Mouse Event Capture

A custom event handler `PickHandler` (inheriting `vsg::Inherit<vsg::Visitor, PickHandler>`):
- Listens for `vsg::ButtonPressEvent` (left mouse button)
- Records click coordinates `(x, y)` in window pixel space

### Readback Flow

1. **On click**: Record pixel coordinates `(x, y)`
2. **After next frame render**: Read pixel at `(x, y)` from offscreen color attachment
3. **Decode**: `faceId = (R << 16) | (G << 8) | B`
4. **Lookup**: Find `faceId` in `faceRegistry` to get `FaceInfo`
5. **Callback**: Notify application via callback with `PickResult`

### Readback Implementation

Use a staging buffer for GPU → CPU pixel transfer:

```cpp
// 1x1 staging buffer for single pixel readback
auto stagingImage = vsg::ubvec4Array2D::create(1, 1);
// Issue GPU → CPU copy for the clicked pixel region
// Wait for fence, then read pixel value from staging buffer
```

### Pick API

```cpp
using PickCallback = std::function<void(const PickResult& result)>;

class PickHandler : public vsg::Inherit<vsg::Visitor, PickHandler> {
public:
    void setPickCallback(PickCallback callback);
    void apply(vsg::ButtonPressEvent& event) override;

private:
    PickCallback _callback;
    vsg::ref_ptr<vsg::Image> _idImage;  // Offscreen color attachment reference
};
```

## Highlight and Information Panel

### Highlight Implementation

On face selection, modify the per-vertex color buffer of the selected face's triangles:

- **Highlight color**: Bright yellow `(1.0, 0.8, 0.2)`
- Use `FaceData.triangleOffset` and `triangleCount` to locate the exact vertex range
- Backup original colors before overwriting; restore on deselection

```cpp
class SelectionManager {
public:
    void selectFace(uint32_t faceId);     // Highlight the specified face
    void clearSelection();                 // Clear highlight, restore original colors
    const PickResult* getSelection() const;

private:
    uint32_t _selectedFaceId = 0;
    vsg::ref_ptr<vsg::vec3Array> _colorArray;   // Reference to main render color array
    std::vector<vsg::vec3> _originalColors;      // Backup of original colors
};
```

### Information Panel (Qt Side)

A floating Qt widget in the overlay area, displaying selected face properties:

```
┌─────────────────────────┐
│ Selected Face            │
│ ─────────────────────── │
│ Face ID:    42           │
│ Part:       Bolt_M10     │
│ Normal:     (0, 0, 1)   │
│ Area:       12.34 mm²   │
└─────────────────────────┘
```

- Hidden when `faceId == 0` (no selection)
- Positioned in the overlay layout, non-overlapping with main view

## Module Boundaries

| Module | Responsibility |
|--------|---------------|
| `mesh` | Face ID assignment, `FaceData` population, `perTriangleFaceId` array |
| `scene` | ID color buffer creation, offscreen pipeline setup, `faceRegistry` population |
| `pick` (new) | `PickHandler` event handler, pixel readback, ID decoding |
| `selection` (new) | `SelectionManager` for highlight state and color buffer manipulation |
| Qt viewer | Information panel widget, wiring pick callback to UI |

## Key Design Decisions

1. **Global face ID (not per-part)**: Simpler encoding, single lookup table, no ambiguity
2. **Per-frame offscreen render (not on-demand)**: Eliminates click-time latency; cost is acceptable for typical CAD model sizes
3. **RGB encoding (not R32_UINT)**: Standard format, well-supported by VSG, visually debuggable
4. **Vertex color modification for highlight (not separate overlay geometry)**: Reuses existing data, no extra draw calls
5. **No MSAA on pick buffer**: Essential for ID precision — multisampling blends colors between faces
