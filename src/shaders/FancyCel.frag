#version 410 core

uniform vec4 u_color;

uniform vec3 u_cel_dark_color;
uniform vec3 u_cel_bright_color;
uniform vec3 u_cel_light_dir;
uniform vec3 u_cel_light_pos;
uniform float u_cel_light_type; // 0 = directional, 1 = point

uniform float u_cel_dark_threshold;
uniform float u_cel_bright_threshold;
uniform float u_cel_shadow_strength;
uniform float u_cel_highlight_strength;
uniform float u_cel_pattern_scale;
uniform float u_cel_pattern_radius;
uniform float u_cel_bands;
uniform float u_cel_flat; // when > 0.5, output u_color directly (unlit)

uniform sampler2D u_texture_1;

in vec4 v_position;
in vec4 v_normal;
in vec2 v_uv;

layout(location = 0) out vec4 out_color;
layout(location = 1) out vec4 out_normal;

vec3 quantizeNormal(vec3 N, float levels) {
  vec3 q = N * 0.5 + 0.5;
  q = floor(q * levels) / levels;
  q = q * 2.0 - 1.0;
  return normalize(q);
}

vec3 textureDistortNormal(vec3 N, vec2 uv, float strength) {
  vec3 tex = texture(u_texture_1, uv).rgb;
  vec3 offset = tex * 2.0 - 1.0;
  return normalize(N + offset * strength);
}

void main() {
  vec3 N_raw = normalize(v_normal.xyz);

  vec2 uv = v_uv * u_cel_pattern_scale;

  // Slightly bend the normal with texture first.
  // This helps break overly regular bands on smooth objects like spheres.
  vec3 N = textureDistortNormal(N_raw, uv, 0.08);

  if (u_cel_flat > 0.5) {
    out_color = u_color;
    out_normal = vec4(N * 0.5 + 0.5, 1.0);
    return;
  }

  vec3 L = (u_cel_light_type > 0.5)
           ? normalize(u_cel_light_pos - v_position.xyz)
           : normalize(u_cel_light_dir);

  vec4 tri_pattern = texture(u_texture_1, uv);

  float ndotl_raw = max(dot(N, L), 0.0);

  // Use the texture to slightly shift the lighting value,
  // so the cel bands are less geometrically regular.
  float pattern_shift = tri_pattern.r - 0.5;
  ndotl_raw += pattern_shift * 0.08;
  ndotl_raw = clamp(ndotl_raw, 0.0, 1.0);

  // Quantize lighting instead of quantizing the normal.
  float bands = max(1.0, u_cel_bands);
  float ndotl = floor(ndotl_raw * bands) / bands;

  float ramp = clamp(ndotl / max(u_cel_dark_threshold, 1e-3), 0.0, 1.0);
  float lit_quantized = floor(ramp * bands) / bands;

  float dark_mask = 1.0 - lit_quantized;
  float dark_blend_scalar = dark_mask * u_cel_shadow_strength;
  float dark_T = clamp(tri_pattern.r * dark_blend_scalar, 0.0, 1.0);

  vec3 shaded = mix(u_color.rgb, u_cel_dark_color, dark_T);

  float bright_mask = step(u_cel_bright_threshold, ndotl_raw);
  vec3 highlight = bright_mask * u_cel_bright_color * u_cel_highlight_strength;

  vec3 final_color = min(shaded + highlight, vec3(1.0));

  out_color = vec4(final_color, u_color.a);
  out_normal = vec4(N * 0.5 + 0.5, 1.0);
}