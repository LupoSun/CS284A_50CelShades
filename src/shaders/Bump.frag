#version 330

uniform vec3 u_cam_pos;
uniform vec3 u_light_pos;
uniform vec3 u_light_intensity;

uniform vec4 u_color;

uniform sampler2D u_texture_2;
uniform vec2 u_texture_2_size;

uniform float u_normal_scaling;
uniform float u_height_scaling;

in vec4 v_position;
in vec4 v_normal;
in vec4 v_tangent;
in vec2 v_uv;

out vec4 out_color;

float h(vec2 uv) {
  // You may want to use this helper function...
  vec3 tex = texture(u_texture_2, uv).rgb;
  return (tex.r + tex.g + tex.b) / 3.0;
}

void main() {
  // YOUR CODE HERE
  
  // TAO START
  vec3 n = normalize(v_normal.xyz);
  vec3 t = normalize(v_tangent.xyz);
  vec3 b = normalize(cross(n, t));
  mat3 TBN = mat3(t, b, n);

  float du = (h(v_uv + vec2(1.0 / u_texture_2_size.x, 0.0)) - h(v_uv))
             * u_height_scaling * u_normal_scaling;
  float dv = (h(v_uv + vec2(0.0, 1.0 / u_texture_2_size.y)) - h(v_uv))
             * u_height_scaling * u_normal_scaling;

  vec3 n_local = normalize(vec3(-du, -dv, 1.0));
  vec3 n_displaced = normalize(TBN * n_local);

  vec3 ka = vec3(0.1);
  vec3 kd = vec3(1.0);
  vec3 ks = vec3(0.5);
  vec3 Ia = vec3(1.0);
  float p = 32.0;

  vec3 light_dir = u_light_pos - v_position.xyz;
  float r = length(light_dir);
  vec3 l = normalize(light_dir);
  vec3 v = normalize(u_cam_pos - v_position.xyz);
  vec3 hv = normalize(l + v);

  vec3 ambient  = ka * Ia;
  vec3 diffuse  = kd * (u_light_intensity / (r * r)) * max(0.0, dot(n_displaced, l));
  vec3 specular = ks * (u_light_intensity / (r * r)) * pow(max(0.0, dot(n_displaced, hv)), p);

  out_color = vec4(ambient + diffuse + specular, 1.0);
  // TAO END
}
