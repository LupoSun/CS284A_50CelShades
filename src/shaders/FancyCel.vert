#version 410 core

uniform mat4 u_model;
uniform mat4 u_view_projection;

in vec4 in_position;
in vec4 in_normal;
in vec4 in_tangent;
in vec2 in_uv;

out vec4 v_position;
out vec4 v_normal;
out vec2 v_uv;
out vec4 v_tangent;

void main() {
  mat3 model_3x3 = mat3(u_model);

  v_position = u_model * in_position;
  v_normal = vec4(normalize(model_3x3 * in_normal.xyz), 0.0);
  v_uv = in_uv;
  v_tangent = vec4(normalize(model_3x3 * in_tangent.xyz), 0.0);

  gl_Position = u_view_projection * u_model * in_position;
}
