#version 330 core

uniform float time;
uniform vec2 resolution;

in vec4 out_color;
in vec2 out_uv;

out vec4 frag_color;

vec3 hsl2rgb(vec3 c) {
    vec3 rgb = clamp(abs(mod(c.x * 6.0 + vec3(0.0,4.0,2.0), 6.0)-3.0)-1.0, 0.0, 1.0);
    return c.z + c.y * (rgb-0.5)*(1.0-abs(2.0*c.z-1.0));
}

void main() {
    vec2 frag_uv = out_uv;
    vec4 rainbow = vec4(hsl2rgb(vec3((time + frag_uv.x + frag_uv.y), 0.5, 0.5)), 1.0);
    frag_color = vec4(rainbow.rgb, out_color.a);
}