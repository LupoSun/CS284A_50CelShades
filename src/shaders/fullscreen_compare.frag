#version 410 core

in vec2 v_uv;
out vec4 out_color;

uniform sampler2D u_leftTex;
uniform sampler2D u_rightTex;
uniform float u_split;
uniform float u_compare_mode;
uniform float u_divider_width;
uniform vec3 u_divider_color;

void main() {
  float half_width = max(u_divider_width * 0.5, 0.0005);

  if (u_compare_mode > 0.5) {
    if (abs(v_uv.x - 0.5) <= half_width) {
      out_color = vec4(u_divider_color, 1.0);
      return;
    }

    if (v_uv.x < 0.5) {
      out_color = texture(u_leftTex, vec2(v_uv.x * 2.0, v_uv.y));
    } else {
      out_color = texture(u_rightTex, vec2((v_uv.x - 0.5) * 2.0, v_uv.y));
    }
    return;
  }

  if (abs(v_uv.x - u_split) <= half_width) {
    out_color = vec4(u_divider_color, 1.0);
    return;
  }

  vec3 left_color = texture(u_leftTex, v_uv).rgb;
  vec3 right_color = texture(u_rightTex, v_uv).rgb;
  out_color = vec4(v_uv.x <= u_split ? left_color : right_color, 1.0);
}
