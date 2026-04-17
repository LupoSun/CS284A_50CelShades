# Cel Shader + Live GUI Devlog

This doc is a handoff for anyone picking up the Cel shader or wanting to tune it
in the cloth simulator. It covers what the shader does, the GUI we added for
live tweaking, and how presets travel between people (and eventually Blender).

## TL;DR

- Shader files: [src/shaders/Cel.vert](../src/shaders/Cel.vert), [src/shaders/Cel.frag](../src/shaders/Cel.frag)
- Live controls: **Appearance** window → **Cel Shader** group
- Save/load: **Save**, **Save As**, **Load** buttons write/read JSON presets
- Default preset location: `src/scene/cel_preset.json` (auto-loaded on startup)
- Default active shader is now **Cel** (falls back to Wireframe if Cel fails to compile)

## What the shader does

The Cel shader is a port of the Unity Shader Graph we prototyped on the
whiteboard. It produces a multi-band toon look with an optional cotton-weave
halftone visible only in the shadow transition.

Pipeline (see [Cel.frag](../src/shaders/Cel.frag)):

1. **Triplanar sample** of `u_texture_1` (the cotton pattern) using world-space
   position + normal, tiled by `u_cel_pattern_scale`. Triplanar removes the
   need for proper UVs — it projects the texture from the three cardinal axes
   and blends by the normal.
2. **NdotL** = `dot(worldNormal, worldLightDir)`. `u_cel_light_dir` is expected
   as surface-to-light (already negated relative to the Unity "Main Light
   Direction" node, which points the other way).
3. **Posterized dark band**: we remap NdotL through `u_cel_dark_threshold`
   (that's where "fully lit" starts), then quantize into `u_cel_bands`
   discrete levels via `floor()`. This is what gives you multiple shadow tones
   instead of a binary light/dark split. The resulting dark mask is multiplied
   by the triplanar pattern, so the cotton weave appears only in shadow.
4. **Highlight band**: `step(u_cel_bright_threshold, NdotL) * brightColor *
   highlightStrength` — a crisp rim that gets added on top.
5. **Final** = `mix(baseColor, darkColor, darkT) + highlight`, clamped to 1.

The posterization step is the key difference from a textbook cel shader. A
single `step()` gives you two tones; `floor(ramp * bands) / bands` gives you
`bands + 1` tones while keeping the hard boundaries cel shading is known for.

## Uniforms → GUI controls

All Cel parameters are live-editable. Everything here is per-frame, no rebuild
needed.

| Uniform                     | GUI label         | Range        | Notes                                                    |
| --------------------------- | ----------------- | ------------ | -------------------------------------------------------- |
| `u_color` (base color)      | Color (ColorWheel)| RGBA         | Same widget as before                                    |
| `u_cel_dark_color`          | Dark color        | RGBA         | ColorPicker popup                                        |
| `u_cel_bright_color`        | Bright color      | RGBA         | ColorPicker popup                                        |
| `u_cel_dark_threshold`      | Dark thresh       | 0–1          | Where "fully lit" begins                                 |
| `u_cel_bright_threshold`    | Bright thresh     | 0–1          | Where the highlight rim begins                           |
| `u_cel_shadow_strength`     | Shadow str        | 0–1          | How strongly the dark color replaces the base in shadow  |
| `u_cel_highlight_strength`  | Highlight str     | 0–2          | Scales the highlight contribution                        |
| `u_cel_pattern_scale`       | Pattern scale     | 0.05–30      | Triplanar tiling; `1.0` matches the Unity `tiling=1`     |
| `u_cel_bands`               | Bands             | 1–16         | Number of posterized shadow levels (int-ish)             |
| `u_cel_light_dir`           | Light dir x/y/z   | free         | Auto-normalized each frame                               |

`u_cel_pattern_radius` is still in the header for future use but the shader
path no longer consumes it (left over from the older procedural-dot version).

## Save / Load preset

We added three buttons below the Cel Shader panel.

- **Save** → writes `src/scene/cel_preset.json`. Overwrites existing.
- **Save As** → native file dialog, pick any `.json` path.
- **Load** → native file dialog, read any preset and refresh widgets in place.

Preset JSON shape (see `ClothSimulator::saveCelPreset`):

```json
{
  "base_color":        [0.85, 0.35, 0.40, 1.0],
  "dark_color":        [0.38, 0.16, 0.18, 1.0],
  "bright_color":      [1.0,  0.85, 0.78, 1.0],
  "light_dir":         [0.5,  2.0,  2.0],
  "dark_threshold":    0.42,
  "bright_threshold":  0.95,
  "shadow_strength":   0.9,
  "highlight_strength":0.18,
  "pattern_scale":     1.0,
  "pattern_radius":    0.18,
  "bands":             4.0
}
```

Unknown/missing keys are ignored, so we can extend the schema later without
breaking older presets. On startup, the constructor tries
`src/scene/cel_preset.json`; if it exists it overrides the hardcoded defaults
from `clothSimulator.h`.

**Workflow for sharing presets:** tweak in GUI → **Save** → commit
`src/scene/cel_preset.json` to git. Anyone who pulls will see your look on
their next launch.

## Running the sim

From `src/build/`:

```sh
./clothsim -f ../scene/sphere.json
```

Or with an explicit project root (the one that contains `shaders/Default.vert`
— note it's `src/`, **not** `src/src/`):

```sh
./clothsim -r .. -f ../scene/sphere.json
```

## Notes on the C++ side

- [clothSimulator.h](../src/src/clothSimulator.h): new `m_cel_*` member
  variables hold all the tweakable state; `m_refresh_cel_widgets` is an
  std::function the load path calls to push values back into widgets.
- [clothSimulator.cpp](../src/src/clothSimulator.cpp):
  - `saveCelPreset` / `loadCelPreset` / `defaultCelPresetPath`
  - `drawContents()` PHONG branch uploads all `u_cel_*` uniforms from members
    instead of literals.
  - `initGUI()` builds the Cel Shader panel and collects refresh callbacks
    into a shared vector; each callback pushes the current member value back
    into its widget (so **Load** actually updates the visible numbers).
- `load_shaders()` prefers "Cel" as the default active shader, falling back
  to "Wireframe" if Cel didn't compile.

## Blender port

The plan is for the Blender side to consume the same JSON preset so the look
stays consistent across the sim and the final render. The shader math is
straightforward to map to Blender's node graph (or a GLSL node in EEVEE Next).
Keep the preset as the source of truth; if you change param semantics, update
this doc and both implementations.

## Known gaps / follow-ups

- The triplanar uses `u_texture_1` (whatever `textures/texture_1.png` is). If
  we want a dedicated cotton texture we should add a new uniform + texture
  slot rather than repurposing slot 1.
- `u_cel_bands` is a float for GUI convenience; the shader does `floor()` so
  non-integer values are clipped anyway. Fine for now, but a real int box
  would be tidier.
- The Light direction controls are three separate boxes. A small 3-axis
  widget or a spherical-coord pair would be friendlier.
