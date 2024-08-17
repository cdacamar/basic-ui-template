#version 330 core

uniform float time;
uniform vec2 resolution;

in vec4 out_color;
in vec2 out_uv;

out vec4 frag_color;

void main() {
    vec2 frag_uv = out_uv;
    vec4 color = out_color;

    float dist = 1.0 - length(frag_uv);
    dist = smoothstep(0.0, 0.005, dist);
    color.a = dist;

    frag_color = color;
}