## Export to Unity HLSL

Click **Export HLSL** in the button row to generate a Unity `.shader` file
(Built-in render pipeline). All current GUI values are written as `Properties`
defaults so the Unity inspector starts from your exact look.

The exported file targets Unity's **Built-in pipeline** via `UnityCG.cginc`.
To adapt to **URP**, the main changes are:

- Replace `CGPROGRAM / ENDCG` with `HLSLPROGRAM / ENDHLSL`
- Replace `#include "UnityCG.cginc"` with `#include "Packages/com.unity.render-pipelines.universal/ShaderLibrary/Core.hlsl"`
- Replace `UnityObjectToClipPos` → `TransformObjectToHClip`
- Replace `UnityObjectToWorldNormal` → `TransformObjectToWorldNormal`
- Replace `tex2D(tex, uv)` → `SAMPLE_TEXTURE2D(_MainTex, sampler_MainTex, uv)`

The math inside `frag()` is identical in both pipelines — only the wiring
changes.

**Workflow:** tune in cloth sim GUI → **Export HLSL** → drop `.shader` into
your Unity `Assets/Shaders/` folder → assign to a material → assign the cotton
texture to `_MainTex`.

The exported shader **is not** the same as the original Unity Shader Graph. It
is a hand-translated HLSL equivalent. If you need the Shader Graph version,
use the whiteboard diagram as reference — the node structure maps 1:1 to the
HLSL logic in the export.

## Blender port

The plan is for the Blender side to consume the same JSON preset so the look
stays consistent across the sim and the final render. The shader math is
straightforward to map to Blender's node graph (or a GLSL node in EEVEE Next).
Keep the preset as the source of truth; if you change param semantics, update
this doc and both implementations.




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