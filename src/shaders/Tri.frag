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
uniform float u_cel_flat;

in vec4 v_position;
in vec4 v_normal;
in vec2 v_uv;

layout(location = 0) out vec4 out_color;
layout(location = 1) out vec4 out_normal;

float hash1(float n) {
  return fract(sin(n) * 43758.5453123);
}

float hash2(vec2 p) {
  return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453123);
}

vec3 hash3(float n) {
  return vec3(
    hash1(n + 11.1),
    hash1(n + 37.7),
    hash1(n + 91.3)
  );
}

vec3 quantizeColor(vec3 c, float levels) {
  return floor(c * levels) / levels;
}

vec2 chooseSurfaceUV(vec3 pos, vec3 N) {
  vec3 an = abs(N);

  if (an.x > an.y && an.x > an.z) {
    return pos.yz;
  } else if (an.y > an.z) {
    return pos.xz;
  } else {
    return pos.xy;
  }
}

float valueNoise(vec2 p) {
  vec2 i = floor(p);
  vec2 f = fract(p);

  float a = hash2(i);
  float b = hash2(i + vec2(1.0, 0.0));
  float c = hash2(i + vec2(0.0, 1.0));
  float d = hash2(i + vec2(1.0, 1.0));

  vec2 u = f * f * (3.0 - 2.0 * f);

  return mix(mix(a, b, u.x), mix(c, d, u.x), u.y);
}

float getTriangleId(vec2 uv, float scale) {
  // Warp UV a little so the triangle grid is less regular.
  vec2 warp;
  warp.x = valueNoise(uv * 1.7 + vec2(4.2, 1.3));
  warp.y = valueNoise(uv * 1.7 + vec2(8.4, 6.1));
  warp = (warp - 0.5) * 0.55;

  vec2 p = (uv + warp) * scale;

  // Triangular / skewed grid.
  mat2 skew = mat2(
    1.0, 0.0,
    0.5, 0.8660254
  );

  vec2 q = skew * p;

  vec2 cell = floor(q);
  vec2 f = fract(q);

  float flip = step(0.5, hash2(cell));

  float triA = step(f.x, f.y);
  float triB = step(1.0 - f.x, f.y);
  float tri = mix(triA, triB, flip);

  return cell.x * 157.0 + cell.y * 311.0 + tri * 911.0;
}

vec3 getPatchNormal(float patch_id, vec3 base_N) {
  vec3 r = hash3(patch_id) * 2.0 - 1.0;

  // Keep random direction roughly related to the surface.
  r = normalize(r);
  vec3 N = normalize(mix(base_N, r, 0.65));

  return N;
}

void main() {
  vec3 N_raw = normalize(v_normal.xyz);
  vec3 pos = v_position.xyz;

  // Smaller = larger triangles. Larger = smaller triangles.
  float patch_scale = 10.0;

  vec2 surface_uv = chooseSurfaceUV(pos, N_raw);
  float patch_id = getTriangleId(surface_uv, patch_scale);

  // This normal is constant per triangle id, so color/shadow is triangle-based.
  vec3 N = getPatchNormal(patch_id, N_raw);

  if (u_cel_flat > 0.5) {
    out_color = u_color;
    out_normal = vec4(N * 0.5 + 0.5, 1.0);
    return;
  }

  vec3 L = (u_cel_light_type > 0.5)
           ? normalize(u_cel_light_pos - pos)
           : normalize(u_cel_light_dir);

  float ndotl = max(dot(N, L), 0.0);

  // One lighting offset per triangle.
  ndotl += (hash1(patch_id * 2.13) - 0.5) * 0.20;
  ndotl = clamp(ndotl, 0.0, 1.0);

  float bands = max(1.0, u_cel_bands);
  float ndotl_quantized = floor(ndotl * bands) / bands;

  float ramp = clamp(
    ndotl_quantized / max(u_cel_dark_threshold, 1e-3),
    0.0,
    1.0
  );

  float dark_mask = 1.0 - ramp;
  float dark_T = clamp(dark_mask * u_cel_shadow_strength, 0.0, 1.0);

  vec3 final_color = mix(u_color.rgb, u_cel_dark_color, dark_T);

  float bright_mask = step(u_cel_bright_threshold, ndotl);
  final_color += bright_mask * u_cel_bright_color * u_cel_highlight_strength;

  // One value/tint per triangle.
  float value_variation = mix(0.86, 1.14, hash1(patch_id));
  vec3 tint_variation = mix(
    vec3(0.94),
    vec3(1.06),
    hash3(patch_id * 0.31)
  );

  final_color *= value_variation;
  final_color *= tint_variation;

  final_color = quantizeColor(final_color, 16.0);
  final_color = clamp(final_color, 0.0, 1.0);

  out_color = vec4(final_color, u_color.a);
  out_normal = vec4(N * 0.5 + 0.5, 1.0);
}