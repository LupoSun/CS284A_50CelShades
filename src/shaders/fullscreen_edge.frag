#version 410 core

in vec2 v_uv;
out vec4 out_color;

uniform sampler2D u_colorTex;
uniform sampler2D u_normalTex;
uniform sampler2D u_depthTex;

uniform float u_edge_thickness;
uniform float u_depth_threshold;
uniform float u_normal_threshold;
uniform float u_edge_strength;
uniform vec3 u_edge_color;

bool isBackground(float d) {
  return d >= 0.9999;
}

float getDepthEdge(vec2 uv, vec2 texelSize) {
  vec2 dx = vec2(texelSize.x * u_edge_thickness, 0.0);
  vec2 dy = vec2(0.0, texelSize.y * u_edge_thickness);

  float dC = texture(u_depthTex, uv).r;
  float dL = texture(u_depthTex, uv - dx).r;
  float dR = texture(u_depthTex, uv + dx).r;
  float dU = texture(u_depthTex, uv + dy).r;
  float dD = texture(u_depthTex, uv - dy).r;

  bool bgC = isBackground(dC);
  bool bgL = isBackground(dL);
  bool bgR = isBackground(dR);
  bool bgU = isBackground(dU);
  bool bgD = isBackground(dD);

  if (bgC) {
    return ((!bgL) || (!bgR) || (!bgU) || (!bgD)) ? 1.0 : 0.0;
  }

  if (bgL || bgR || bgU || bgD) {
    return 1.0;
  }

  return 0.0;
}

float getNormalEdge(vec2 uv, vec2 texelSize) {
  vec2 dx = vec2(texelSize.x * u_edge_thickness, 0.0);
  vec2 dy = vec2(0.0, texelSize.y * u_edge_thickness);

  float dC = texture(u_depthTex, uv).r;
  if (isBackground(dC)) {
    return 0.0;
  }

  vec3 nC = texture(u_normalTex, uv).rgb * 2.0 - 1.0;
  vec3 nL = texture(u_normalTex, uv - dx).rgb * 2.0 - 1.0;
  vec3 nR = texture(u_normalTex, uv + dx).rgb * 2.0 - 1.0;
  vec3 nU = texture(u_normalTex, uv + dy).rgb * 2.0 - 1.0;
  vec3 nD = texture(u_normalTex, uv - dy).rgb * 2.0 - 1.0;

  float diffL = 1.0 - dot(nC, nL);
  float diffR = 1.0 - dot(nC, nR);
  float diffU = 1.0 - dot(nC, nU);
  float diffD = 1.0 - dot(nC, nD);

  float maxDiff = max(
    max(diffL, diffR),
    max(diffU, diffD)
  );

  return step(u_normal_threshold, maxDiff);
}

void main() {
  vec3 baseColor = texture(u_colorTex, v_uv).rgb;

  vec2 texSize = vec2(textureSize(u_colorTex, 0));
  vec2 texelSize = 1.0 / texSize;

  float depthEdge = getDepthEdge(v_uv, texelSize);
  float normalEdge = getNormalEdge(v_uv, texelSize);

  float edge = max(depthEdge, normalEdge * 0.6);
  edge = clamp(edge * u_edge_strength, 0.0, 1.0);

  if (edge > 0.5) {
    out_color = vec4(u_edge_color, 1.0);
  } else {
    out_color = vec4(baseColor, 1.0);
  }
}