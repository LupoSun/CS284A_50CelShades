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

uniform float u_use_mtl_style; // 0 = default color mode, 1 = mesh/mtl-style mode

uniform sampler2D u_texture_1;

uniform mat4 u_light_view_projection;
uniform sampler2D u_shadow_map;
uniform float u_shadow_bias;
uniform float u_shadow_strength;

in vec4 v_position;          // world position
in vec4 v_object_position;   // object/local position
in vec4 v_normal;
in vec2 v_uv;
in vec4 v_color;
in vec4 v_tangent;

layout(location = 0) out vec4 out_color;
layout(location = 1) out vec4 out_normal;

mat2 rotate2D(float a) {
  float c = cos(a);
  float s = sin(a);
  return mat2(c, -s, s, c);
}

vec2 getUnifiedPlanarUV(vec3 objPos, float scale) {
  vec2 uv = objPos.xy;
  uv = rotate2D(0.0) * uv;
  return uv * scale;
}

vec4 samplePatternUnified(vec3 objPos, float scale) {
  vec2 uv = getUnifiedPlanarUV(objPos, scale);
  vec2 dx = dFdx(uv);
  vec2 dy = dFdy(uv);
  return textureGrad(u_texture_1, uv, dx, dy);
}

float computeShadow(vec3 N, vec3 L) {
  vec4 lightClip = u_light_view_projection * v_position;
  vec3 projCoords = lightClip.xyz / lightClip.w;
  projCoords = projCoords * 0.5 + 0.5;

  // Outside the valid shadow-map region
  if (projCoords.x < 0.0 || projCoords.x > 1.0 ||
      projCoords.y < 0.0 || projCoords.y > 1.0 ||
      projCoords.z < 0.0 || projCoords.z > 1.0) {
    return 0.0;
  }

  float ndotlRaw = max(dot(N, L), 0.0);
  float bias = max(u_shadow_bias * (1.0 - ndotlRaw), 0.0005);

  // Simple 3x3 PCF
  vec2 texelSize = 1.0 / vec2(textureSize(u_shadow_map, 0));
  float shadow = 0.0;

  for (int x = -1; x <= 1; x++) {
    for (int y = -1; y <= 1; y++) {
      vec2 offset = vec2(float(x), float(y)) * texelSize;
      float closestDepth = texture(u_shadow_map, projCoords.xy + offset).r;
      shadow += (projCoords.z - bias > closestDepth) ? 1.0 : 0.0;
    }
  }

  shadow /= 9.0;
  return shadow;
}

void main() {
  vec3 N = normalize(v_normal.xyz);

  // -----------------------------
  // base color
  // -----------------------------
  vec3 base_color = u_color.rgb;

  // Only use mesh/material color when MTL style is enabled
  if (u_use_mtl_style > 0.5 && length(v_color.rgb) > 0.001) {
    base_color = v_color.rgb;
  }

  if (u_cel_flat > 0.5) {
    out_color = vec4(base_color, u_color.a);
    out_normal = vec4(N * 0.5 + 0.5, 1.0);
    return;
  }

  // IMPORTANT:
  // For directional light, u_cel_light_dir is treated as the direction
  // light travels FROM the light TO the scene.
  // So the vector from surface point TO light is -u_cel_light_dir.
  vec3 L = (u_cel_light_type > 0.5)
           ? normalize(u_cel_light_pos - v_position.xyz)
           : normalize(-u_cel_light_dir);

  float ndotl = max(dot(N, L), 0.0);

  float shadow = 0.0;
  if (u_cel_light_type < 0.5) {
    shadow = computeShadow(N, L);
  }

  float bands = max(1.0, u_cel_bands);
  float ramp = clamp(ndotl / max(u_cel_dark_threshold, 1e-3), 0.0, 1.0);
  float lit_quantized = floor(ramp * bands) / bands;

  float dark_mask = 1.0 - lit_quantized;
  float dark_T = clamp(dark_mask * u_cel_shadow_strength, 0.0, 1.0);

  // Let real shadow directly deepen the cel dark region
  dark_T = clamp(dark_T + shadow * u_shadow_strength, 0.0, 1.0);

  vec3 shaded = mix(base_color, u_cel_dark_color, dark_T);

  // MTL-style pattern only for mesh/material objects
if (u_use_mtl_style > 0.5) {
  vec4 pattern_tex = samplePatternUnified(v_object_position.xyz, u_cel_pattern_scale);

  float line_value = pattern_tex.r;
  float line_mask = smoothstep(0.35, 0.85, line_value);

  // 1) original deepest cel band
  float deepest_cel_mask = 1.0 - step(0.001, lit_quantized);

  // 2) real shadow region
  float shadow_mask = step(0.05, shadow);

  // allow texture in either case
  float texture_region_mask = max(deepest_cel_mask, shadow_mask);

  line_mask *= texture_region_mask;

  float texture_strength = 0.8;

  vec3 line_target = mix(shaded, base_color, texture_strength);
  shaded = mix(shaded, line_target, line_mask);
}

  float bright_mask = step(u_cel_bright_threshold, ndotl);

  // Suppress highlight in shadowed areas
  bright_mask *= (1.0 - shadow);

  vec3 highlight = bright_mask * u_cel_bright_color * u_cel_highlight_strength;

  float keep_tangent_active = v_tangent.x * 0.0;

  vec3 final_color = min(shaded + highlight, vec3(1.0));

  out_color = vec4(final_color + keep_tangent_active, u_color.a);
  out_normal = vec4(N * 0.5 + 0.5, 1.0);
}