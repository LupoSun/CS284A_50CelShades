# Split Compare + Mesh Loading Devlog

This entry documents the presentation features added after the cel shader and
edge-pass milestone: split-screen comparison, OBJ/MTL mesh loading, runtime mesh
loading and deletion from the GUI, optional cloth rendering/simulation, and
optional mesh collision.

## Summary

- Split comparison mode renders the same synced simulation twice: cel-shaded on
  the left and a Phong baseline on the right.
- The comparison view supports two layouts:
  - **Wipe**: a movable vertical boundary controlled by the Boundary slider.
  - **Side-by-side**: two fixed panes, both rendered with the correct half-pane
    aspect ratio so the scene is not horizontally squeezed.
- OBJ/MTL loading now supports material diffuse colors, scale, translation,
  relative path resolution, polygon fan triangulation, common OBJ face index
  formats, and negative OBJ indices.
- Meshes can be loaded from scene JSON or hot-loaded through the Appearance GUI.
- Mesh-only scenes are valid. If a scene omits the `cloth` block, the simulator
  skips cloth construction and frames the camera around the loaded render
  objects.
- Cloth can be hidden/paused at runtime with the **Cloth** toggle in the
  Simulation panel.
- Mesh collision is optional and controlled by a GUI toggle. It is off by
  default because triangle sweep collision is slower than the existing primitive
  collision objects.
- Cel edges are controlled by **Edge** -> **Cel edges**, and the default
  `src/scene/cel_preset.json` is loaded automatically at startup.

## User-Facing Workflow

Launch from `src/build`:

```sh
./clothsim -r .. -f ../scene/example_mesh.json
```

In the **Appearance** panel:

- Scroll the panel to reach the lower controls.
- Use **Mesh** -> **Load OBJ** to browse for a `.obj` file at runtime.
- Set mesh **Scale**, **Trans**, and **Friction** before loading.
- Use **Mesh** -> **Loaded mesh** to select a GUI-loaded mesh, then **Delete**
  or Backspace to remove it from the current session. Scene JSON meshes are not
  listed or deleted by this control.
- Use **Mesh** -> **Collide** to enable or disable collision for all loaded
  meshes.
- Use **Edge** -> **Cel edges** to show or hide the fullscreen outline pass.
- Use **Compare** -> **Split view** to turn comparison rendering on.
- Use **Compare** -> **Layout** to switch between **Wipe** and
  **Side-by-side**.
- In **Wipe**, the **Boundary** slider moves the cel/baseline divider.
- In **Side-by-side**, the divider is fixed at the center and the slider is
  intentionally ignored.
- Use **Simulation** -> **Cloth** to show/resume or hide/pause the cloth when a
  cloth exists in the scene. Mesh-only scenes show this as unavailable.

Mesh-only demo launch:

```sh
./clothsim -r .. -f ../scene/mesh_only.json
```

## Rendering Pipeline

When comparison mode is off and cloth is enabled, rendering follows the existing
cel/edge path:

```text
simulate cloth
  -> render active shader into offscreen color/normal/depth buffers
  -> run fullscreen edge pass
  -> draw to default framebuffer
```

When comparison mode is on:

```text
simulate cloth once
  -> render cel scene into the offscreen buffers
  -> run edge pass into a cel-composite texture
  -> render Phong baseline into a second framebuffer
  -> composite cel + Phong in fullscreen_compare.frag
```

If cloth is disabled or absent, the simulation step and cloth draw calls are
skipped. Collision objects and meshes still render through the same shader and
post-process paths.

The side-by-side layout still uses the fullscreen compositor, but the source
renders use the half-pane aspect ratio before compositing. That keeps objects
undistorted after each source image is mapped into half of the window.

## Optional Cloth

The simulator now tracks cloth availability separately from cloth visibility:

- `m_has_cloth` means the scene JSON provided a `cloth` block and `main.cpp`
  built the cloth grid/mesh.
- `m_cloth_enabled` is the runtime GUI toggle.
- `clothActive()` is the guard used by simulation, reset, shadow rendering, and
  material draw helpers.

Scenes without `cloth` are valid. In that case, `main.cpp` does not call
`buildGrid()`, `buildClothMesh()`, `loadCloth()`, or `loadClothParameters()`.
The camera is framed from collision object bounds instead of cloth point masses,
with an origin/default-distance fallback if no bounds are available.

## Mesh Loading

Scene JSON supports both a single `mesh` object and a `meshes` array:

```json
{
  "path": "sample_mesh.obj",
  "friction": 0.0,
  "scale": 1.0,
  "translate": [0.0, 0.0, 0.0],
  "collide": false
}
```

The loader resolves paths in this order:

1. Absolute path as-is.
2. Relative to the scene JSON directory.
3. Relative to the project root.

OBJ support includes `v`, `vt`, `vn`, `f`, `mtllib`, `usemtl`,
`v/vt/vn`, `v//vn`, polygon fan triangulation, and negative indices.

MTL support currently reads `Kd` diffuse colors. Texture maps such as
`map_Kd` remain out of scope.

## Runtime Mesh Loading

The GUI hot-load path uses the same `Mesh` constructor and render/collision
buffers as JSON-loaded meshes. Runtime meshes are appended to the active
collision object list immediately, so they render in the current frame loop
without restarting the simulator.

Runtime mesh controls are intentionally simple:

- Scale defaults to `[1, 1, 1]`.
- Translation defaults to `[0, 0, 0]`.
- Friction defaults to `0`.
- Collision state follows the global Mesh Collide toggle at load time and can
  be changed later with the same toggle.
- GUI-loaded meshes are tracked separately from scene JSON meshes. The
  **Loaded mesh** selector and Backspace deletion only affect this runtime list.
- Deleting a runtime mesh removes it from both the GUI runtime list and the
  active collision/render object list, then deletes the mesh object.

## Mesh Collision

Mesh collision is implemented as an optional point-mass segment sweep against
mesh triangles:

1. For each point mass, test the segment from `last_position` to `position`.
2. Reject triangles early using a simple triangle AABB.
3. Intersect the segment with the triangle plane.
4. Use barycentric containment to accept hits inside the triangle.
5. Push the point mass to a small normal offset and apply friction similarly to
   the plane collision path.

There is no BVH yet. This is acceptable for the current presentation target
because collision is off by default and can be enabled only when needed.

## Files Touched

- `src/src/clothSimulator.cpp` / `.h`
  - Split comparison state, framebuffer flow, GUI controls, scrollable
    Appearance panel, runtime mesh loading/deletion, side-by-side projection
    correction, optional cloth guards, and mesh-only camera framing.
- `src/shaders/fullscreen_compare.frag`
  - Fullscreen compositor for wipe and side-by-side layouts.
- `src/src/collision/mesh.cpp` / `.h`
  - OBJ/MTL parsing, material colors, render buffers, tangent/color uploads,
    optional triangle collision, and mesh bounds.
- `src/src/main.cpp`
  - Scene JSON mesh parsing, path resolution, and cloth presence detection.
- `src/src/collision/collisionObject.h`
  - Mesh capability hooks, object bounds, and virtual destructor.
- `src/src/collision/plane.cpp` / `sphere.h`
  - Tangent buffer compatibility for the cel shader path and primitive bounds.
- `src/scene/example_mesh.json`
  - Valid mesh demo scene.
- `src/scene/mesh_only.json`
  - Mesh-only demo scene with no `cloth` block.
- `src/scene/cel_preset.json`
  - Tracked default cel preset loaded on startup.
- `src/scene/sample_mesh.obj` / `.mtl`
  - Small tracked sample asset for demos.

## Validation

Validated with:

```sh
cd src/build
cmake --build .
```

Also checked:

```sh
git diff --check
python3 -m json.tool src/scene/cel_preset.json
python3 -m json.tool src/scene/mesh_only.json
```

Manual runtime checks performed during development:

- `example_mesh.json` loads without the old `Invalid scene object found: meshes`
  error after rebuilding the correct `src/build` executable.
- Mesh material diffuse colors appear in the cel path.
- The Appearance panel scrolls after the control list became taller than the
  window.
- Wipe comparison keeps the movable boundary behavior.
- Side-by-side comparison keeps the scene undistorted.
- Runtime OBJ loading appends a mesh without restarting.
- Runtime mesh deletion removes GUI-loaded meshes without affecting scene JSON
  meshes.
- Mesh-only scenes launch without cloth and frame around render objects.
- The default cel preset loads automatically and refreshes the visible edge
  toggle.

## Remaining Follow-Ups

- Add per-mesh collision toggles if scenes start using many loaded assets.
- Add `map_Kd` texture support if textured OBJ assets become part of the final
  presentation.
- Add acceleration structure support if mesh collision needs to scale beyond
  small demo meshes.
- Add a clear-all runtime mesh button if repeated mesh iteration becomes common.
