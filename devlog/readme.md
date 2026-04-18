# Cel Shader + Edge pipeline

This doc is a handoff for anyone picking up the Cel shader or wanting to tune it
in the cloth simulator. It covers what the shader does, the GUI we added for
live tweaking, and how presets travel between people.



## Summary

- Shader files: [src/shaders/Cel.vert](../src/shaders/Cel.vert), [src/shaders/Cel.frag](../src/shaders/Cel.frag)
- The Cel shader is implemented as the first shading pass: it computes the toon-shaded base color from lighting and the UV-space pattern texture, and also writes an encoded normal buffer for the later edge pass.

- Edge files: [src/shaders/fullscreen_edge.vert](../src/shaders/fullscreen_edge.vert), [src/shaders/fullscreen_edge.frag](../src/shaders/fullscreen_edge.frag)
- Edge is implemented as a separate fullscreen post-process shader: the scene is first rendered into a framebuffer that stores color, depth, and normal information, and then the edge shader uses the depth and normal discontinuities to detect and draw outlines.

- GUI: the GUI is working, but there are still some settings/default value inconsistency in the code. As it doesn't influence the current stage, I will work on this afterwards :)
  - **Pattern tex**: Now there are two texture imgs provided in the textures folder, each of them may work well with different settings. Feel free to play around!
  - **Edge on/of button**: As the edge is generated as a separate shader, it is controled by a separate button on the right bottom corner. It can work with any shader.

- Possible next steps:
  - Add shadows
  - See how it works with other more complex scene
  - ...



## Render pipeline

```text
load shaders + textures
  → init()
    → create offscreen framebuffer (FBO)
      → color buffer
      → normal buffer
      → depth buffer
    → create fullscreen quad

each frame: drawContents()
  → update cloth simulation

  → Pass 1: Cel shading pass
    → bind offscreen FBO
    → clear color + depth
    → bind Cel shader
    → set uniforms
    → bind pattern textures
    → draw cloth geometry
    → Cel.frag
      → sample pattern texture in UV space
      → compute NdotL
      → quantize into cel bands
      → apply dark pattern in shadow bands
      → add highlight band
      → output shaded color to color buffer
      → output encoded normal to normal buffer
    → depth written to depth buffer

  → Pass 2: Fullscreen edge pass
    → bind default framebuffer
    → clear background
    → bind fullscreen edge shader
    → bind FBO textures as inputs
      → color
      → normal
      → depth
    → draw fullscreen quad
    → fullscreen_edge.frag
      → read scene color
      → compare neighboring depth samples
      → detect outer silhouette
      → compare neighboring normal samples
      → detect internal feature lines
      → composite edge color over scene color

  → final image on screen
```


## What the Cel shader does

The Cel shader is the first pass of the pipeline. It computes the base toon shading of the cloth and writes both the final shaded color and an encoded normal buffer.

Pipeline (see [Cel.frag](../src/shaders/Cel.frag)):

1. **UV-space pattern sample** of `u_texture_1`, tiled by `u_cel_pattern_scale`.
2. **NdotL** = `dot(worldNormal, worldLightDir)`.
3. **Posterized shadow bands**: NdotL is remapped by `u_cel_dark_threshold` and quantized into `u_cel_bands` discrete levels using `floor()`.
4. **Pattern-masked shadowing**: the sampled texture is multiplied by the dark-band mask and `u_cel_shadow_strength`, so the pattern only appears in the darker bands.
5. **Highlight band**: `step(u_cel_bright_threshold, NdotL) * brightColor * highlightStrength`.
6. **Outputs**:

   * the final cel-shaded color
   * an encoded normal buffer for the later edge pass



## What the fullscreen edge shader does

The fullscreen edge shader is the second pass of the pipeline. It reads the buffers generated in the first pass and detects outlines in screen space.

Pipeline (see [fullscreen_edge.frag](../src/shaders/fullscreen_edge.frag)):

1. The scene is first rendered into an offscreen framebuffer storing:

   * **color**
   * **depth**
   * **normal**
2. The edge shader runs as a **separate fullscreen post-process pass**.
3. For each screen pixel, it samples the current pixel and neighboring pixels from the **depth** and **normal** buffers.
4. **Depth discontinuities** are used to detect outer silhouettes and object-background boundaries.
5. **Normal discontinuities** are used to detect internal feature lines, such as folds or sharp local changes across the surface.
6. The resulting edge mask is combined with the first-pass color buffer, so outlines are drawn on top of the cel-shaded result.




## Running the sim

From `src/build/`:

```sh
./clothsim -f ../scene/sphere.json
```


## Notes on the C++ side

- [clothSimulator.h](../src/src/clothSimulator.h): 
  - **Cel**: new `m_cel_*` member
  variables hold all the tweakable state; `m_refresh_cel_widgets` is an
  std::function the load path calls to push values back into widgets.
  - **Edge**: added the offscreen rendering resources for the two-pass outline pipeline, including framebuffer / color / normal / depth textures, fullscreen quad buffers, the separate fullscreen edge shader, and related helper functions such as `initFramebuffer()`, `resizeFramebuffer()`, and `drawFullscreenQuad()`.
- [clothSimulator.cpp](../src/src/clothSimulator.cpp):
  - **Cel**: 
    - `saveCelPreset` / `loadCelPreset` / `defaultCelPresetPath`
    - `exportCelHLSL(path)` — streams a complete `.shader` file with current
      member values substituted into the Properties block and HLSL body
    - `drawContents()` PHONG branch uploads all `u_cel_*` uniforms from members
      instead of literals.
    - `initGUI()` builds the Cel Shader panel and collects refresh callbacks
      into a shared vector; each callback pushes the current member value back
      into its widget (so **Load** actually updates the visible numbers).
    - `load_shaders()` prefers "Cel" as the default active shader, falling back
      to "Wireframe" if Cel didn't compile.
  - **Edge**:
    * initialized the offscreen framebuffer, fullscreen quad, and fullscreen edge shader in `init()`.
    * updated `drawContents()` from a single-pass render into a two-pass pipeline: first render the scene into the offscreen framebuffer, then run a separate fullscreen edge pass using the stored color / depth / normal buffers.
    * added explicit texture rebinding before the scene draw so the cel shader samples the correct pattern textures in pass 1.
    * updated resize handling so the framebuffer attachments are recreated when the window size changes.



## Known gaps / follow-ups

- `u_cel_bands` is a float for GUI convenience; the shader does `floor()` so
  non-integer values are clipped anyway. Fine for now, but a real int box
  would be tidier.
- The Light direction controls are three separate boxes. A small 3-axis
  widget or a spherical-coord pair would be friendlier.
