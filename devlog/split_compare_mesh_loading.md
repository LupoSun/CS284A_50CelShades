# Split Compare + Mesh Loading Devlog

This entry documents the presentation features added after the cel shader and
edge-pass milestone: split-screen comparison, OBJ/MTL mesh loading, runtime mesh
loading from the GUI, and optional mesh collision.

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
- Mesh collision is optional and controlled by a GUI toggle. It is off by
  default because triangle sweep collision is slower than the existing primitive
  collision objects.

## User-Facing Workflow

Launch from `src/build`:

```sh
./clothsim -r .. -f ../scene/example_mesh.json
```

In the **Appearance** panel:

- Scroll the panel to reach the lower controls.
- Use **Mesh** -> **Load OBJ** to browse for a `.obj` file at runtime.
- Set mesh **Scale**, **Trans**, and **Friction** before loading.
- Use **Mesh** -> **Collide** to enable or disable collision for all loaded
  meshes.
- Use **Compare** -> **Split view** to turn comparison rendering on.
- Use **Compare** -> **Layout** to switch between **Wipe** and
  **Side-by-side**.
- In **Wipe**, the **Boundary** slider moves the cel/baseline divider.
- In **Side-by-side**, the divider is fixed at the center and the slider is
  intentionally ignored.

## Rendering Pipeline

When comparison mode is off, rendering follows the existing cel/edge path:

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

The side-by-side layout still uses the fullscreen compositor, but the source
renders use the half-pane aspect ratio before compositing. That keeps objects
undistorted after each source image is mapped into half of the window.

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
    Appearance panel, runtime mesh loading, side-by-side projection correction.
- `src/shaders/fullscreen_compare.frag`
  - Fullscreen compositor for wipe and side-by-side layouts.
- `src/src/collision/mesh.cpp` / `.h`
  - OBJ/MTL parsing, material colors, render buffers, tangent/color uploads,
    optional triangle collision.
- `src/src/main.cpp`
  - Scene JSON mesh parsing and path resolution.
- `src/src/collision/collisionObject.h`
  - Mesh capability hooks and virtual destructor.
- `src/src/collision/plane.cpp`
  - Tangent buffer compatibility for the cel shader path.
- `src/scene/example_mesh.json`
  - Valid mesh demo scene.
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

## Remaining Follow-Ups

- Add a remove/clear button for runtime-loaded meshes.
- Add per-mesh collision toggles if scenes start using many loaded assets.
- Add `map_Kd` texture support if textured OBJ assets become part of the final
  presentation.
- Add acceleration structure support if mesh collision needs to scale beyond
  small demo meshes.
