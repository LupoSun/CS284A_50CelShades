#version 410 core

uniform vec4 u_color;

uniform vec3 u_cel_dark_color;
uniform vec3 u_cel_bright_color;
uniform vec3 u_cel_light_dir;

uniform float u_cel_dark_threshold;
uniform float u_cel_bright_threshold;
uniform float u_cel_shadow_strength;
uniform float u_cel_highlight_strength;
uniform float u_cel_pattern_scale;
uniform float u_cel_pattern_radius;
uniform float u_cel_bands;

uniform sampler2D u_texture_1;

in vec4 v_position;
in vec4 v_normal;
in vec2 v_uv;

layout(location = 0) out vec4 out_color;
layout(location = 1) out vec4 out_normal;

void main() {
  vec3 N = normalize(v_normal.xyz);
  vec3 L = normalize(u_cel_light_dir);

  vec2 uv = v_uv * u_cel_pattern_scale;
  vec4 tri_pattern = texture(u_texture_1, uv);

  float ndotl = max(dot(N, L), 0.0);

  float bands = max(1.0, u_cel_bands);
  float ramp = clamp(ndotl / max(u_cel_dark_threshold, 1e-3), 0.0, 1.0);
  float lit_quantized = floor(ramp * bands) / bands;

  float dark_mask = 1.0 - lit_quantized;
  float dark_blend_scalar = dark_mask * u_cel_shadow_strength;
  float dark_T = clamp(tri_pattern.r * dark_blend_scalar, 0.0, 1.0);

  vec3 shaded = mix(u_color.rgb, u_cel_dark_color, dark_T);

  float bright_mask = step(u_cel_bright_threshold, ndotl);
  vec3 highlight = bright_mask * u_cel_bright_color * u_cel_highlight_strength;

  vec3 final_color = min(shaded + highlight, vec3(1.0));
  out_color = vec4(final_color, u_color.a);
  out_normal = vec4(N * 0.5 + 0.5, 1.0);
}