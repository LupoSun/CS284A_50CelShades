#version 330


uniform vec3 u_cam_pos;

uniform samplerCube u_texture_cubemap;

in vec4 v_position;
in vec4 v_normal;
in vec4 v_tangent;

out vec4 out_color;

void main() {
  // YOUR CODE HERE
  // TAO START
  vec3 n = normalize(v_normal.xyz);

  vec3 wo = v_position.xyz - u_cam_pos;

  vec3 wi = reflect(wo, n);

  out_color = texture(u_texture_cubemap, wi);
  // TAO END
}
