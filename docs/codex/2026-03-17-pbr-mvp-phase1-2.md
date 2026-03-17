# PBR MVP Phase 1-2 Development Log

Date: 2026-03-17

## Goal

Implement the agreed PBR MVP skeleton:

- Phase 1: add CAD-side visual material data with STEP/XCAF import
- Phase 2: add `Legacy / Pbr` face shading paths in scene building
- Keep existing picking, selection, and hover working
- Leave textures, IBL, and highlight/material decoupling for later work

## Development Notes

### 1. CAD import: add `ShapeVisualMaterial`

Updated `StepReader` so each `cad::ShapeNode` now carries both:

- `ShapeNodeColor`
- `ShapeVisualMaterial`

`ShapeVisualMaterial` stores:

- `baseColorFactor`
- `emissiveFactor`
- `metallicFactor`
- `roughnessFactor`
- `alphaCutoff`
- `alphaMask`
- `doubleSided`
- `hasPbr`
- `source`

Import priority is:

1. explicit XCAF visual material
2. color fallback from `XCAFDoc_ColorTool`
3. library default material

`STEPCAFControl_Reader::SetMatMode(true)` is now enabled so STEP/XCAF material data can flow into the document.

### 2. Scene options: add shading mode switch

Added:

- `scene::ShadingMode { Legacy, Pbr }`
- `SceneOptions::shadingMode`
- `SceneOptions::addHeadlight`

This keeps the default behavior backward-compatible while allowing PBR builds explicitly.

### 3. Face rendering: split legacy and PBR paths

`SceneBuilder` now has two face paths:

- `createLegacyFaceNode()`
- `createPbrFaceNode()`

Implementation choice for this MVP:

- use VSG's official `createPhysicsBasedRenderingShaderSet()`
- pass metallic / roughness / emissive through `vsg::PbrMaterial`
- keep base color in the dynamic vertex color array

This was intentional so the existing selection/highlight system continues to work without a full overlay refactor yet.

### 4. Highlight compatibility

Face colors were migrated from `vec3Array` to `vec4Array`.

This keeps:

- part highlight
- face highlight
- hover highlight
- selection layering

working in both `Legacy` and `Pbr` modes.

Current limitation:

- highlight still mutates face vertex color directly
- imported material data is preserved in `PartSceneNode::visualMaterial`, but visual highlight is not yet decoupled from shading output

That decoupling remains the next major rendering task.

### 5. Viewer integration

The example viewer now supports:

- default legacy mode
- `--pbr` startup switch

Examples:

```powershell
.\build\examples\vsgqt_step_viewer\Debug\vsgqt_step_viewer.exe D:\path\model.step
.\build\examples\vsgqt_step_viewer\Debug\vsgqt_step_viewer.exe --pbr D:\path\model.step
```

The window title and default status text now display the active shading mode.

## Verification

Build checked with:

```powershell
cmake --build D:\vsgOcct\build --config Debug --target vsgocct test_step_reader test_scene_builder vsgqt_step_viewer
```

Full test suite checked with:

```powershell
ctest --test-dir D:\vsgOcct\build -C Debug --output-on-failure
```

Result:

- `40 / 40` tests passed

## Files Touched

- `include/vsgocct/cad/StepReader.h`
- `src/vsgocct/cad/StepReader.cpp`
- `include/vsgocct/scene/SceneBuilder.h`
- `src/vsgocct/scene/SceneBuilder.cpp`
- `include/vsgocct/StepModelLoader.h`
- `src/vsgocct/StepModelLoader.cpp`
- `examples/vsgqt_step_viewer/main.cpp`
- `tests/test_step_reader.cpp`
- `tests/test_scene_builder.cpp`

## Known Gaps

- no texture import yet
- no HDR / IBL environment lighting yet
- no tonemapping controls yet
- no in-viewer runtime shading toggle yet; current switch is startup-only via `--pbr`
- face highlight is still color-mutation based, not overlay/outline based

## Recommended Next Steps

1. decouple highlight from base material output
2. add runtime `Legacy / Pbr` toggle in the viewer
3. add texture slots and material caching
4. add environment lighting and exposure controls
