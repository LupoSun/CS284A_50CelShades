#version 410 core

uniform mat4 u_model;
uniform mat4 u_light_view_projection;

in vec4 in_position;
in vec4 in_normal;
in vec2 in_uv;
in vec4 in_tangent;

void main() {
    float keep_attributes = 0.0;
    keep_attributes += in_normal.x * 0.0;
    keep_attributes += in_uv.x * 0.0;
    keep_attributes += in_tangent.x * 0.0;

    gl_Position = u_light_view_projection * u_model * in_position;
    gl_Position.x += keep_attributes;
}