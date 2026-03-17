# Viewer Material Preset Window

Date: 2026-03-17

## Goal

Add a material selection window to `vsgqt_step_viewer` so the loaded model can switch between several common material presets at runtime.

Requested presets:

- Iron
- Copper
- Gold
- Wood
- Acrylic

Assumption made for usability:

- an extra `Imported` option is also provided so the viewer can return to the original STEP/XCAF material or color state

## Implementation Summary

### 1. Runtime material state in scene data

`PartSceneNode` now keeps both:

- `importedMaterial`
- `visualMaterial`

and also stores:

- `pbrMaterialValue`

This makes it possible to update runtime material parameters without rebuilding the scene.

### 2. Material preset API

Added to `scene::SceneBuilder`:

- `MaterialPreset`
- `materialPresetName()`
- `makeMaterialPreset()`
- `applyMaterialPreset()`

`applyMaterialPreset()` updates:

- active part material data
- part base face color
- bound `PbrMaterialValue`
- face colors used by the current highlight system

After the update it rebuilds the current highlight layering, so existing `selected` / `hover` state stays visually correct.

### 3. Viewer material window

Added a right-side dock window in `vsgqt_step_viewer`:

- title: `材质`
- combo box for preset selection
- description text for the active preset

The presets act on the whole loaded model.

### 4. Viewer default mode change

Because preset switching is much more meaningful under PBR, the example viewer now defaults to `PBR`.

Compatibility fallback:

- `--legacy` forces legacy shading mode

Legacy mode still allows preset switching, but only the base color effect is reliable there.

## Preset Values

The current presets are intentionally simple, stable defaults:

- Iron: cool gray, high metallic, medium roughness
- Copper: warm copper, high metallic, lower roughness
- Gold: bright gold, high metallic, low roughness
- Wood: brown dielectric, zero metallic, high roughness
- Acrylic: pale translucent dielectric, low roughness, double-sided

These are viewer presets, not physically measured material libraries.

## Verification

Build checked with:

```powershell
cmake --build D:\vsgOcct\build --config Debug --target vsgocct test_scene_builder vsgqt_step_viewer
```

Full test suite checked with:

```powershell
ctest --test-dir D:\vsgOcct\build -C Debug --output-on-failure
```

Result:

- `42 / 42` tests passed

## Files Touched

- `include/vsgocct/scene/SceneBuilder.h`
- `src/vsgocct/scene/SceneBuilder.cpp`
- `examples/vsgqt_step_viewer/main.cpp`
- `tests/test_scene_builder.cpp`

## Next Steps

1. allow per-part material overrides instead of only whole-model presets
2. add color chip / roughness / metallic sliders next to the presets
3. separate highlight from face base color so material editing and highlight rendering are fully independent
4. add texture-backed presets after the texture import path is ready
