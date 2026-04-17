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

out vec4 out_color;

vec4 triplanarSample(sampler2D tex, vec3 pos_ws, vec3 normal_ws, float tiling) {
  vec3 weights = max(abs(normal_ws), vec3(1e-5));
  weights /= (weights.x + weights.y + weights.z);

  vec4 sample_x = texture(tex, pos_ws.yz * tiling);
  vec4 sample_y = texture(tex, pos_ws.xz * tiling);
  vec4 sample_z = texture(tex, pos_ws.xy * tiling);

  return sample_x * weights.x + sample_y * weights.y + sample_z * weights.z;
}

void main() {
  vec3 N = normalize(v_normal.xyz);
  vec3 L = normalize(u_cel_light_dir);

  vec4 tri_pattern = triplanarSample(u_texture_1, v_position.xyz, N, u_cel_pattern_scale);

  float ndotl = dot(N, L);

  float bands = max(1.0, u_cel_bands);
  float ramp = clamp(ndotl / max(u_cel_dark_threshold, 1e-3), 0.0, 1.0);
  float lit_quantized = floor(ramp * bands) / bands;
  float dark_mask = 1.0 - lit_quantized;
  float dark_blend_scalar = dark_mask * u_cel_shadow_strength;
  vec4 dark_blend = tri_pattern * dark_blend_scalar;
  float dark_T = clamp(dark_blend.r, 0.0, 1.0);

  vec3 shaded = mix(u_color.rgb, u_cel_dark_color, dark_T);

  float bright_mask = step(u_cel_bright_threshold, ndotl);
  vec3 bright_value = bright_mask * u_cel_bright_color;
  vec3 highlight = bright_value * u_cel_highlight_strength;

  out_color = vec4(min(shaded + highlight, vec3(1.0)), u_color.a);
}
