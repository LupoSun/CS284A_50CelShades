# Implementation Reference

This document covers the five main systems in the cloth simulator: the cel
shader, shadow mapping, fullscreen edge detection, OBJ mesh loading, and the
split-screen compare compositor. `clothSimulator.cpp` is the hub that drives
all of them every frame.

---

## 1. Main Pipeline (`clothSimulator.cpp` / `clothSimulator.h`)

### What happens each frame

`drawContents()` is the per-frame entry point. It runs the following steps
in order:

1. **Simulation.** If the simulation is not paused and cloth is active,
   `cloth->simulate()` advances the cloth by `simulation_steps` sub-steps.
   The cloth being "active" means `m_has_cloth && m_cloth_enabled && cloth !=
   nullptr` — both the scene having a cloth block and the GUI toggle being on.

2. **Light rotation.** If `m_light_rotate` is enabled, the light direction
   and position are updated each frame using `glfwGetTime()`, orbiting the
   light around the Y axis at `m_light_rotate_speed`.

3. **Shadow map.** `getLightViewProjectionMatrix()` builds an orthographic
   projection from `m_cel_light_dir`. Then `renderShadowMap()` uses that to
   render all geometry into a depth-only framebuffer. This step is skipped
   when point light mode is active, because the system only supports a single
   directional shadow frustum.

4. **Scene rendering.** This differs depending on whether compare mode is on.
   - **Compare OFF:** `renderSceneToOffscreen` renders the active shader into
     the offscreen FBO, then `renderEdgeComposite` runs the edge pass and
     writes the result directly to the default framebuffer.
   - **Compare ON:** the same scene is rendered twice — once with the Cel
     shader (result composited with edges into `m_cel_composite_fbo`), and
     once with the Phong baseline shader (into `m_baseline_fbo`). Then
     `renderCompareComposite` blends those two results together using the
     compare shader. When side-by-side layout is active, both renders use a
     half-pane aspect ratio so neither image is distorted when each occupies
     only half the window.

### How scene geometry is drawn (`renderSceneGeometry`)

This helper is shared by all render passes. It binds the active shader and
uploads the model matrix (identity) and view-projection. For Phong-hint
shaders (which includes both Cel and Phong), it also uploads camera position,
light parameters, and binds the four loaded textures plus the cubemap. When
the active shader is Cel with `cel_materials` enabled, it additionally uploads
all the `u_cel_*` uniforms from the simulator's member variables and binds the
shadow depth texture on unit 6. For each loaded mesh that has an MTL file,
it sets `u_use_mtl_style = 1` so the shader uses the per-vertex diffuse color
instead of the uniform base color. In point light mode, a small yellow sphere
is drawn at `m_cel_light_pos` using flat (unlit) mode to mark the light.

### Framebuffer layout

The simulator maintains four custom framebuffers alongside the default one:

- `m_offscreen_fbo` is the primary offscreen target. It has two color
  attachments — one for the shaded color and one for packed world-space
  normals — plus a depth buffer. This is what `renderSceneToOffscreen` writes
  into, and what the edge pass reads from.
- `m_cel_composite_fbo` stores the cel-shaded result after the edge pass.
  It is only used during compare mode as the left-side source.
- `m_baseline_fbo` stores the Phong baseline for the right side of the
  compare view.
- `m_shadow_fbo` is a 4096×4096 depth-only framebuffer used for shadow map
  generation.

### State in `clothSimulator.h`

All live-tweakable parameters are stored as member variables so the GUI can
modify them at any time without a shader recompile. The cel shader parameters
(`m_cel_dark_color`, `m_cel_bands`, `m_cel_light_type`, etc.) are uploaded as
uniforms each frame inside `renderSceneGeometry`. Edge parameters
(`m_depth_edge_thickness`, `m_edge_normal_threshold`, etc.) are similarly
passed each frame to the edge shader. Compare state (`m_compare_enabled`,
`m_compare_layout`, `m_compare_split`) controls which render path is taken in
`drawContents`.

---

## 2. Cel Shader ([src/shaders/Cel.vert](../src/shaders/Cel.vert) / [Cel.frag](../src/shaders/Cel.frag))

### Vertex shader

`Cel.vert` computes four varyings that the fragment shader depends on.
`v_position` is the world-space position, used for the shadow map lookup and
the point-light direction vector. `v_object_position` is the untransformed
object-space position, used for the pattern texture projection. `v_normal` is
the world-space normal computed by multiplying the input normal by the upper
3×3 of the model matrix. `v_color` carries the per-vertex diffuse color
uploaded by the mesh loader from MTL data. The tangent is kept in the output
only to prevent the driver from stripping the `in_tangent` attribute — it is
not actually used in lighting.

### Fragment shader — step by step

**Base color.** The fragment starts with the uniform `u_color` as its base
color. If `u_use_mtl_style` is set and the incoming `v_color` is non-black,
the per-vertex MTL diffuse color overrides the uniform. This is how mesh
materials show their own colors without needing a separate shader variant.

**Flat mode.** If `u_cel_flat` is set, the shader outputs the base color
unmodified and returns early. This mode is only used when drawing the point
light indicator sphere, to render it in flat yellow without any shading.

**Light vector.** Depending on `u_cel_light_type`, the light direction `L` is
either a constant direction (`-u_cel_light_dir`, for directional light) or a
per-fragment vector from the surface to `u_cel_light_pos` (for point light).
NdotL is then a standard `dot(N, L)` clamped to zero.

**Shadow lookup.** For directional light only, `computeShadow` projects the
world-space position into the shadow map's clip space using
`u_light_view_projection`. Fragments outside the shadow frustum return zero
shadow immediately. A slope-scale bias is derived from `tan(acos(NdotL))`
scaled by `u_shadow_bias` and clamped to `[0.0005, 0.01]` — this adjusts the
bias based on how oblique the surface is to the light, reducing both
self-shadowing acne and peter-panning. The comparison is done as a 4-tap PCF
kernel at ±0.75 texel diagonal offsets, and the result is softened with
`smoothstep(0.35, 0.65)` to give the shadow edge a slight gradient instead of
a hard step.

**Toon ramp (posterization).** The NdotL value is first normalized against
`u_cel_dark_threshold` to get a 0–1 ramp, then quantized into `u_cel_bands`
discrete steps using `floor(ramp * bands) / bands`. A band count of 1 gives
a classic two-tone cel look; higher counts add more shadow gradation while
keeping hard boundaries between steps. The quantized value is then inverted to
get a `dark_mask` — how much of the dark color should replace the base.

**Dark and shadow blending.** The dark mask scaled by `u_cel_shadow_strength`
drives a `mix` from base color toward `u_cel_dark_color`. The shadow map
result is then blended in on top of that, also toward the dark color. So a
surface can be dark for two independent reasons: being in the unlit cel band,
or being occluded by another object's shadow.

**Cotton pattern.** This step only runs when `u_use_mtl_style` is on. The
pattern texture is sampled by projecting the object-space XY position (scaled
by `u_cel_pattern_scale`) as a planar 2D UV — not triplanar, just a flat XY
projection. The resulting line mask is multiplied by a region mask that limits
the pattern to shadow and dark-cel areas only (excluding fully lit and bright
highlight regions). This means the cotton weave texture is only visible in the
shadow transition zones, adding a fabric-like detail without cluttering the
lit areas.

**Highlight.** A crisp additive rim is fired wherever `NdotL` exceeds
`u_cel_bright_threshold`, weighted by `u_cel_highlight_strength` and the
bright color. It is suppressed in shadowed regions by multiplying by
`(1.0 - shadow)`, so cast shadows can cut through highlights.

**Dual render targets.** The shader writes to two output locations: the
composited color to `out_color` (location 0), and the world-space normal
packed into `[0, 1]` to `out_normal` (location 1). The normal buffer is what
the edge detection pass reads from `m_scene_normal_tex`.

### Uniform → GUI mapping

| Uniform | GUI label | Notes |
|---|---|---|
| `u_color` | Color | Base; overridden by MTL `Kd` per vertex |
| `u_cel_dark_color` | Dark color | Shadow / unlit tone |
| `u_cel_bright_color` | Bright color | Additive highlight |
| `u_cel_dark_threshold` | Dark thresh | NdotL value where fully lit begins |
| `u_cel_bright_threshold` | Bright thresh | NdotL where rim fires |
| `u_cel_shadow_strength` | Shadow str | Influence of dark color in unlit areas |
| `u_cel_highlight_strength` | Highlight str | 0–2 scale on the rim |
| `u_cel_pattern_scale` | Pattern scale | XY tiling of cotton texture |
| `u_cel_bands` | Bands | Posterization step count (1–16) |
| `u_cel_light_dir` / `u_cel_light_pos` | Light dir / Light pos | Auto-normalized each frame |
| `u_cel_light_type` | Light type | 0 = directional (with shadows), 1 = point |

### Preset serialization

The simulator saves and loads all cel parameters as a JSON file via
`saveCelPreset` and `loadCelPreset`. Unknown keys are silently ignored, so
older presets remain valid after new parameters are added. The default preset
at `src/scene/cel_preset.json` is loaded once during construction and once
more after `initGUI` finishes, which ensures the GUI widgets display the
saved values rather than the hardcoded defaults.

---

## 3. Shadow Mapping ([src/shaders/shadow_depth.vert](../src/shaders/shadow_depth.vert)) NEW!

### The depth-only pass

Shadow rendering is a two-pass technique. In the first pass, the scene is
rendered from the light's point of view into a depth-only framebuffer. In the
second pass (inside `Cel.frag`), each fragment projects itself into that same
light space and compares its depth to what was stored, determining whether it
is in shadow.

`shadow_depth.vert` is kept intentionally minimal. Its only job is to
transform each vertex position by `u_light_view_projection * u_model`. It
does declare `in_normal`, `in_uv`, and `in_tangent` even though it ignores
them — this matches the standard VAO attribute layout used by all other
geometry draw calls, avoiding attribute binding mismatches when switching
shaders.

### Shadow map setup

At init time, a 4096×4096 depth texture is attached to `m_shadow_fbo`. The
Cel shader reads raw depth values from this texture rather than using OpenGL's
built-in shadow comparison mode, because doing the comparison manually allows
finer control over the bias calculation.

### The render pass

Before rendering, front-face culling is enabled (`glCullFace(GL_FRONT)`).
This is the standard trick to reduce self-shadowing acne: since shadow
artifacts appear on front-facing surfaces, culling them from the shadow map
means only back faces contribute depth, and the resulting shadow is slightly
inset from the surface. The pass renders both cloth (when active) and all
collision objects. It is skipped entirely when the light is in point mode,
because the current system only supports a single directional shadow frustum.

### Slope-scale bias in the Cel shader

Inside `Cel.frag`, the shadow bias is not a constant — it scales with
`tan(acos(NdotL))`, which grows as the surface becomes more oblique to the
light. The bias is clamped to `[0.0005, 0.01]` to avoid both
self-shadowing on shallow surfaces and visible peter-panning (detached shadows)
on steep ones.

---

## 4. Edge / Boundary Detection ([src/shaders/fullscreen_edge.frag](../src/shaders/fullscreen_edge.frag) / [.vert](../src/shaders/fullscreen_edge.vert))

### The fullscreen pass

Edge detection runs as a post-process after the scene is rendered. The vertex
shader (`fullscreen_edge.vert`) is a simple pass-through that maps a
fullscreen quad into clip space. The same vertex shader is reused for the
compare compositor. The fragment shader reads the color, normal, and depth
textures from `m_offscreen_fbo` and decides, per pixel, whether that pixel
lies on an edge.

### Depth edges

The depth test samples a 5-tap cross pattern (center plus 4 cardinal
neighbors) at `u_depth_edge_thickness` texel offsets. Background pixels
(depth ≥ 0.9999) are handled specially: if the center is background but a
neighbor is geometry, an edge is drawn (the silhouette seen from the empty
side). If the center is geometry but a neighbor is background, an edge is
also drawn (the silhouette seen from the object side). For two geometry
pixels, an edge fires if the maximum depth difference between center and
neighbors exceeds `u_depth_threshold`. The depth thickness and normal
thickness are separated into two independent parameters so the user can tune
silhouette thickness and interior crease thickness independently.

### Normal edges

The normal test also uses a 5-tap cross at `u_normal_edge_thickness` offsets,
but it is intentionally skipped for background pixels and for any pixel whose
neighbors include background. This avoids double-drawing silhouette edges that
the depth pass already caught. For interior pixels, the test computes
`1 - dot(centerNormal, neighborNormal)` for each of the four neighbors — a
value of 0 means the normals agree, and higher values indicate a surface
crease. If the maximum neighbor deviation exceeds `u_normal_threshold`, the
pixel is marked as an edge.

### Combining and suppressing noise

The raw edge score is `max(depthEdge, normalEdge * 0.6)`. Normal edges are
down-weighted by 0.6 to prevent noisy interiors on curved surfaces from
producing thick false edges. The result then goes through a tiny-edge
suppression filter: an edge pixel is only kept if at least 2 of the 8 pixels
in its 3×3 neighborhood are also edges. This eliminates single-pixel sparkle
that appears on low-quality normals or near grazing angles.

The final output is binary — a pixel is either fully the configured edge
color or fully the pass-through cel color. No anti-aliasing blending is
applied, which preserves the hand-drawn line feel.

### C++ wiring (`renderEdgeComposite`)

`renderEdgeComposite` takes a target FBO as its argument. When compare mode
is off, `target_fbo` is 0 (the default framebuffer), so the edge result goes
directly to screen. When compare mode is on, it is `m_cel_composite_fbo`, and
the result is held for the compare compositor. If the edge pass is disabled or
failed to compile, the function simply blits the raw color attachment from the
offscreen FBO to the target with no processing.

---

## 5. Mesh Loading ([src/src/collision/mesh.cpp](../src/src/collision/mesh.cpp) / [.h](../src/src/collision/mesh.h)) NEW!

### Overview

`Mesh` is a subclass of `CollisionObject`. Its constructor takes a path,
friction, per-axis scale, translation, and a collision-enabled flag. On
construction it calls `loadOBJ` to parse the file, then `buildRenderBuffers`
to pack that data for the GPU and precompute the AABB and per-triangle
collision data.

### OBJ parsing

The parser reads `v`, `vn`, `vt`, `f`, `mtllib`, and `usemtl` directives.
Scale and translation are baked directly into the position during parsing —
each vertex position is transformed as `x*scale.x + translate.x` inline,
so the GPU always sees pre-transformed geometry. Normals are transformed using
the inverse of the scale (dividing each component by its scale factor) and
then re-normalized, which correctly handles non-uniform scaling. Polygon faces
are fan-triangulated: a face with N vertices produces N−2 triangles by
anchoring at vertex 0 and walking the rest. OBJ negative indices (e.g., `-1`
means the last vertex) are resolved to zero-based array indices in
`resolveObjIndex`. The MTL library path is resolved relative to the OBJ
file's directory and loaded immediately when encountered.

### MTL parsing

`loadMTL` reads `newmtl` names and `Kd` (diffuse color) values into a
`material_colors` map. Only diffuse color is supported — `map_Kd` and other
texture directives are currently ignored. When a face's material name is found
in the map, its color is used as the per-vertex color for all three vertices of
that face.

### Render buffer construction

`buildRenderBuffers` packs all parsed triangles into flat Eigen matrices ready
for GPU upload: 4-channel position, 4-channel normal, 2-channel UV, 4-channel
tangent (zeros), and 4-channel per-vertex color. The per-vertex color column
is populated from the material map or left black if no material matched. As a
side effect this function also computes the mesh's axis-aligned bounding box
(stored as `m_bounds_min` / `m_bounds_max`) and pre-caches each triangle's
face normal and AABB for use by the collision system.

### Rendering

`render()` checks each attribute name against the bound shader before
uploading — `in_position`, `in_normal`, `in_uv`, `in_tangent`, `in_color`
are each guarded by `shader.attrib() >= 0`. This makes the mesh renderable
with both the Cel shader (which needs all five) and the shadow depth shader
(which only needs position), without duplicating buffer uploads.

### Collision

The collision algorithm is a segment sweep per point mass. For each triangle,
it first runs a cheap AABB pre-test to reject triangles whose bounding box the
segment cannot possibly intersect. It then checks whether the point mass's
path from `last_position` to `position` crosses the triangle's plane by
looking at the signs of the plane equation evaluated at both endpoints. If
the signs differ, it computes the intersection parameter `t` and uses a
barycentric test to confirm the hit is inside the triangle. If multiple
triangles are hit, the closest one wins. The point mass is then pushed to a
small offset above the surface (`kSurfaceOffset = 0.0001`) in the triangle's
normal direction, with friction applied by scaling the correction by
`(1 - friction)`. There is no acceleration structure — the code iterates all
triangles. This is intentional since collision is off by default and used only
for small demo meshes.

### Scene JSON and runtime loading

Scene files can specify a single `mesh` object or a `meshes` array. Path
resolution tries absolute, then scene-relative, then project-root-relative.
Runtime mesh loading through the GUI calls `loadRuntimeMesh`, which
constructs a `Mesh`, appends it to `collision_objects`, and registers it in
`m_runtime_meshes` for GUI tracking. `deleteSelectedRuntimeMesh` removes the
entry from both the collision list and the GUI list and frees the object.

---

## 6. Split Compare ([src/shaders/fullscreen_compare.frag](../src/shaders/fullscreen_compare.frag)) NEW!

### What it does

The compare compositor is another fullscreen pass that blends two source
textures — the cel-shaded result and a Phong baseline — into a single output
frame. It supports two layouts: a movable wipe boundary and a fixed
side-by-side split.

### Wipe mode

In wipe mode (`u_compare_mode = 0`), the shader samples both textures at the
original full-screen UV and simply picks one based on whether the current
pixel's X coordinate is to the left or right of `u_split`. Because both
sources are sampled at their natural UV, neither is distorted regardless of
where the divider sits. A thin divider line at `u_split` is drawn first,
colored with `u_divider_color` (white by default).

### Side-by-side mode

In side-by-side mode (`u_compare_mode = 1`), the divider is fixed at screen
center. The left half re-maps its UV so that `[0, 0.5]` in screen space
expands back to `[0, 1]` in texture space (`v_uv.x * 2.0`), and the right
half does the same from `[0.5, 1.0]`. This UV expansion works undistorted
because both source images were rendered with a half-pane aspect ratio
(`screen_w/2 / screen_h`) before the compare pass — the geometry was already
told the viewport is half as wide, so re-expanding the UV just undoes that.

### C++ side (`renderCompareComposite`)

The divider line width is passed as `2.0 / screen_w`, which keeps it exactly
one device pixel wide regardless of window size. If `m_compare_animate` is
enabled, the `m_compare_split` position is driven by a sine wave at
`m_compare_anim_speed` to automatically sweep the divider back and forth.
The function binds `m_cel_composite_tex` as the left source and
`m_baseline_color_tex` as the right source.

---

## User-Facing Workflow

Launch from `src/build`:

```sh
./clothsim -r .. -f ../scene/example_mesh.json
```

**Appearance** panel controls (scroll to reach lower sections):

- **Cel Shader**: all toon parameters, Save/Load/Save As preset, Export HLSL.
- **Edge → Cel edges**: enable/disable the fullscreen outline pass.
- **Mesh → Load OBJ**: hot-load a `.obj` at runtime without restarting.
- **Mesh → Scale / Trans / Friction**: configure before loading.
- **Mesh → Loaded mesh + Delete**: manage GUI-loaded meshes; scene JSON meshes
  are not affected.
- **Mesh → Collide**: toggle collision for all meshes globally.
- **Compare → Split view**: enable compare rendering.
- **Compare → Layout**: Wipe (movable boundary) or Side-by-side (fixed halves).
- **Compare → Boundary**: move the wipe divider (ignored in side-by-side).
- **Simulation → Cloth**: show/pause cloth when present; grayed out for
  mesh-only scenes.

---

## Files

| File | Role |
|---|---|
| [src/shaders/Cel.vert](../src/shaders/Cel.vert) | World-space varyings for cel frag |
| [src/shaders/Cel.frag](../src/shaders/Cel.frag) | Toon ramp, shadow lookup, pattern, highlight, dual RT |
| [src/shaders/shadow_depth.vert](../src/shaders/shadow_depth.vert) | Depth-only geometry pass for shadow map |
| [src/shaders/fullscreen_edge.vert](../src/shaders/fullscreen_edge.vert) | Passthrough for fullscreen quad (shared with compare) |
| [src/shaders/fullscreen_edge.frag](../src/shaders/fullscreen_edge.frag) | Depth + normal edge detection, tiny-edge filter |
| [src/shaders/fullscreen_compare.frag](../src/shaders/fullscreen_compare.frag) | Wipe and side-by-side compositor |
| [src/src/clothSimulator.cpp](../src/src/clothSimulator.cpp) | Frame loop, FBO management, all render passes |
| [src/src/clothSimulator.h](../src/src/clothSimulator.h) | Live-tweakable state, FBO/texture handles |
| [src/src/collision/mesh.cpp](../src/src/collision/mesh.cpp) | OBJ/MTL parsing, render buffers, collision |
| [src/src/collision/mesh.h](../src/src/collision/mesh.h) | `Mesh` class declaration |
| [src/scene/cel_preset.json](../src/scene/cel_preset.json) | Default preset (auto-loaded on startup) |
| [src/scene/example_mesh.json](../src/scene/example_mesh.json) | Demo scene with cloth + mesh |

---

## Reminders

- Add a BVH or spatial grid to scale mesh collision beyond small demo assets.
- Add `map_Kd` texture support for textured OBJ files.
- Support point-light shadow mapping — currently the shadow pass is skipped
  entirely when point light mode is active.
- Add per-mesh collision toggles if scenes start mixing collidable and
  decoration-only meshes.
- Add a clear-all runtime mesh button if iterating on many loaded meshes
  becomes common.
