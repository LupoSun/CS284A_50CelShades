#version 330

uniform vec4 u_color;
uniform vec3 u_cam_pos;
uniform vec3 u_light_pos;
uniform vec3 u_light_intensity;

in vec4 v_position;
in vec4 v_normal;
in vec2 v_uv;

out vec4 out_color;

void main() {
  // YOUR CODE HERE
  
  // TAO START
  vec3 ka = vec3(0.1);   // ambient coefficient (low, so ambient is subtle)
  vec3 kd = vec3(1.0);   // diffuse coefficient (white)
  vec3 ks = vec3(0.5);   // specular coefficient (moderate shininess)
  vec3 Ia = vec3(1.0);   // ambient light intensity
  float p = 32.0;        // shininess exponent

  vec3 n = normalize(v_normal.xyz);

  vec3 light_dir = u_light_pos - v_position.xyz;
  float r = length(light_dir);
  vec3 l = normalize(light_dir);

  vec3 v = normalize(u_cam_pos - v_position.xyz);

  vec3 h = normalize(l + v);

  vec3 ambient  = ka * Ia;
  vec3 diffuse  = kd * (u_light_intensity / (r * r)) * max(0.0, dot(n, l));
  vec3 specular = ks * (u_light_intensity / (r * r)) * pow(max(0.0, dot(n, h)), p);

  out_color = vec4(ambient + diffuse + specular, 1.0);
  // TAO END
}

