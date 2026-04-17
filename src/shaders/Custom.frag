#version 330

// (Every uniform is available here.)

uniform mat4 u_view_projection;
uniform mat4 u_model;

uniform float u_normal_scaling;
uniform float u_height_scaling;

uniform vec3 u_cam_pos;
uniform vec3 u_light_pos;
uniform vec3 u_light_intensity;
uniform vec4 u_color;

// Feel free to add your own textures. If you need more than 4,
// you will need to modify the skeleton.
uniform sampler2D u_texture_1;
uniform sampler2D u_texture_2;
uniform sampler2D u_texture_3;
uniform sampler2D u_texture_4;

// Environment map! Take a look at GLSL documentation to see how to
// sample from this.
uniform samplerCube u_texture_cubemap;

in vec4 v_position;
in vec4 v_normal;
in vec4 v_tangent;
in vec2 v_uv;

out vec4 out_color;

void main() {
  // TAO START
  vec3 n = normalize(v_normal.xyz);

  vec3 light_dir = u_light_pos - v_position.xyz;
  float r = length(light_dir);
  vec3 l = normalize(light_dir);

  vec3 v = normalize(u_cam_pos - v_position.xyz);
  vec3 h = normalize(l + v);

  vec3 base_color = u_color.rgb;

  float ndotl = max(0.0, dot(n, l));
  float diffuse_steps = 3.0;
  float toon_diffuse = floor(ndotl * diffuse_steps) / diffuse_steps;

  float specular_strength = pow(max(0.0, dot(n, h)), 64.0);
  float toon_specular = step(0.5, specular_strength);

  vec3 ambient = 0.15 * base_color;
  vec3 diffuse = base_color * (u_light_intensity / (r * r)) * toon_diffuse;
  vec3 specular = vec3(0.25) * (u_light_intensity / (r * r)) * toon_specular;

  out_color = vec4(ambient + diffuse + specular, 1.0);
  // TAO END
}
