#version 410 core

uniform vec4 u_color;

uniform vec3 u_cel_dark_color;
uniform vec3 u_cel_bright_color;
uniform vec3 u_cel_light_dir;
uniform vec3 u_cel_light_pos;
uniform float u_cel_light_type;

uniform float u_cel_dark_threshold;
uniform float u_cel_bright_threshold;
uniform float u_cel_shadow_strength;
uniform float u_cel_highlight_strength;
uniform float u_cel_pattern_scale;
uniform float u_cel_pattern_radius;
uniform float u_cel_bands;
uniform float u_cel_flat;

uniform float u_use_mtl_style;

uniform sampler2D u_texture_1;

uniform mat4 u_light_view_projection;
uniform sampler2D u_shadow_map;
uniform float u_shadow_bias;
uniform float u_shadow_strength;

in vec4 v_position;
in vec4 v_object_position;
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
  if (lightClip.w <= 0.0) return 0.0;
  vec3 projCoords = lightClip.xyz / lightClip.w;
  projCoords = projCoords * 0.5 + 0.5;

  if (projCoords.x < 0.0 || projCoords.x > 1.0 ||
      projCoords.y < 0.0 || projCoords.y > 1.0 ||
      projCoords.z < 0.0 || projCoords.z > 1.0) {
    return 0.0;
  }

  float ndotlRaw = max(dot(N, L), 0.0);

  float slopeBias = u_shadow_bias * tan(acos(clamp(ndotlRaw, 0.0, 1.0)));
  if (u_cel_light_type > 0.5) {
    slopeBias = clamp(slopeBias, 0.0, 0.001);
  } else {
    slopeBias = clamp(slopeBias, 0.0005, 0.01);
  }

  float currentDepth = projCoords.z - slopeBias;

  vec2 texelSize = 1.0 / vec2(textureSize(u_shadow_map, 0));

  float shadow = 0.0;
  for (int x = -1; x <= 1; x++) {
    for (int y = -1; y <= 1; y++) {
      float pcfDepth = texture(u_shadow_map, projCoords.xy + vec2(x, y) * texelSize * 1.5).r;
      shadow += currentDepth > pcfDepth ? 1.0 : 0.0;
    }
  }
  shadow /= 9.0;

  shadow = smoothstep(0.2, 0.8, shadow);

  return shadow;
}
void main() {
  vec3 N = normalize(v_normal.xyz);

  vec3 base_color = u_color.rgb;

  if (u_use_mtl_style > 0.5 && length(v_color.rgb) > 0.001) {
    base_color = v_color.rgb;
  }

  if (u_cel_flat > 0.5) {
    out_color = vec4(base_color, u_color.a);
    out_normal = vec4(N * 0.5 + 0.5, 1.0);
    return;
  }

  vec3 L = (u_cel_light_type > 0.5)
           ? normalize(u_cel_light_pos - v_position.xyz)
           : normalize(-u_cel_light_dir);

  float ndotl = max(dot(N, L), 0.0);

  float shadow = computeShadow(N, L);

  float bands = max(1.0, u_cel_bands);
  float ramp = clamp(ndotl / max(u_cel_dark_threshold, 1e-3), 0.0, 1.0);
  float lit_quantized = floor(ramp * bands) / bands;

  float dark_mask = 1.0 - lit_quantized;
  float dark_T = clamp(dark_mask * u_cel_shadow_strength, 0.0, 1.0);

  vec3 shaded = mix(base_color, u_cel_dark_color, dark_T);

  float shadow_T = clamp(shadow * u_shadow_strength, 0.0, 1.0);
  shaded = mix(shaded, u_cel_dark_color, shadow_T);

  if (u_use_mtl_style > 0.5) {
    vec4 pattern_tex = samplePatternUnified(v_object_position.xyz, u_cel_pattern_scale);

    float line_value = pattern_tex.r;
    float line_mask = 1.0 - smoothstep(0.35, 0.85, line_value);

    float bright_region_mask = step(u_cel_bright_threshold, ndotl);
    float base_band_mask = step(0.999, lit_quantized);
    float dark_cel_mask = (1.0 - base_band_mask) * (1.0 - bright_region_mask);

    float shadow_mask = step(0.35, shadow);

    float texture_region_mask = max(dark_cel_mask, shadow_mask);
    line_mask *= texture_region_mask;

    vec3 shadow_line_color = mix(shaded, u_cel_dark_color, clamp(u_shadow_strength, 0.0, 1.0));
    shaded = mix(shaded, shadow_line_color, line_mask);
  }

  float bright_mask = step(u_cel_bright_threshold, ndotl);
  bright_mask *= (1.0 - shadow);

  vec3 highlight = bright_mask * u_cel_bright_color * u_cel_highlight_strength;

  float keep_tangent_active = v_tangent.x * 0.0;

  vec3 final_color = min(shaded + highlight, vec3(1.0));

  out_color = vec4(final_color + keep_tangent_active, u_color.a);
  out_normal = vec4(N * 0.5 + 0.5, 1.0);
}