# User Guide

For technical implementation details, see [implementation.md](implementation.md).

---

## The Appearance panel

All visual controls are in the scrollable **Appearance** panel on the left.

| Group | What it controls |
|---|---|
| **Cel Shader** | Toon colors, light, shading parameters, pattern, preset save/load, HLSL export |
| **Edge** | Post-process outline toggle and tuning |
| **Mesh** | Runtime OBJ loading, scale/translation/friction, collision |
| **Compare** | Split-screen cel vs. Phong baseline |
| **Simulation** | Cloth visibility, gravity, spring parameters |

---

## Cel Shader


**Light** — **Light type** switches between Directional (supports shadow maps, a parallel light) and Point (positional, no shadow for now). **Rotate light** orbits the light around Y each frame.

**Shading parameters:**
- **Dark thresh** — NdotL value where the lit region begins. Higher = more shadow.
- **Bright thresh** — NdotL where the highlight rim fires. Higher = tighter rim.
- **Shadow str** — how strongly the dark color replaces the base in unlit areas.
- **Highlight str** — scale on the additive highlight (0–2).
- **Bands** — number of discrete shadow steps. 1 = classic two-tone, higher adds gradation steps.

**Pattern** — cotton weave texture tiled across the surface using a flat XY projection in object space. Only visible in shadow/dark regions. **Pattern scale** controls tiling. **Texture** hot-swaps the source image (default: `texture_1.png` in `src/textures/`).

**Preset save / load** — **Save** overwrites `src/scene/cel_preset.json` (commit this to share the look). **Save As** writes to any path. **Load** reads a preset and refreshes all widgets. Unknown keys are ignored.


---

## Edge

**Cel edges** toggles the fullscreen outline pass. When on, a depth + normal discontinuity pass draws outlines over the cel result. **Can turn values to 0 to disable edge**.

- **Depth thickness / Normal thickness** — how many texels the depth and normal tests look outward. Controls silhouette and crease line weight independently.
- **Depth threshold / Normal threshold** — sensitivity of each test. Lower = more edges, more noise.
- **Edge strength** — overall intensity multiplier.
- **Edge color** — color of drawn lines (default black).

---

## Loading meshes

### From a scene JSON

Two ready-to-use meshes are in `src/scene/`: **`mesh_cube.obj`** and **`mesh_scene.obj`**. To switch between them, edit the `path` in `src/scene/example_mesh.json`:

```json
{
  "meshes": [
    { "path": "mesh_scene.obj", "scale": 1.0, "translate": [0,0,0], "friction": 0.0, "collide": false }
  ]
}
```

Paths resolve as: absolute → relative to scene JSON dir → relative to project root. Scenes with no `cloth` block are valid; the camera frames around the meshes instead.

**Coloring via MTL** — place a `.mtl` file with the same base name as the `.obj` in the same directory (e.g. `mesh_scene.mtl` next to `mesh_scene.obj`). The loader reads each material's `Kd` diffuse color and uploads it as per-vertex color, which the Cel shader then uses in place of the uniform base color — so each mesh part keeps its own color while still receiving the toon shading and shadow.

### Runtime loading (GUI)

1. Set **Scale**, **Trans**, and **Friction** in the Mesh group.
2. Click **Load OBJ** and pick a file. The mesh appears immediately.
3. To delete: select it in **Loaded mesh** and click **Delete** or press `Backspace`.

Scene JSON meshes are not listed and cannot be deleted at runtime.

**Collide** toggles triangle collision for all meshes globally (off by default — it's slower than primitive colliders).

---

## Compare view

Turn on **Split view** in the Compare group. The scene renders twice — Cel on the left, Phong on the right.

- **Wipe** (default) — movable divider controlled by the **Boundary** slider. **Animate** sweeps it automatically.
- **Side-by-side** — fixed center split; each half uses a corrected aspect ratio so neither image is distorted.


---

## Keyboard shortcuts

| Key | Action |
|---|---|
| `P` | Pause / resume |
| `N` | Step one frame (while paused) |
| `R` | Reset cloth |
| `Space` | Reset camera |
| `Backspace` | Delete selected runtime mesh |
| `W A S D` / arrows | Pan camera |
| Left drag | Orbit |
| Right drag | Pan |
| Scroll | Zoom |
