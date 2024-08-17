#version 330 core

uniform sampler2D image;

in vec4 out_color;
in vec2 out_uv;

out vec4 frag_color;

void main() {
    vec4 texel = texture(image, out_uv);
    bool adjust = texel.rgb == vec3(0, 0, 0) && texel.a > 0;
    vec4 adjusted_color = out_color;
    adjusted_color.a = texel.a;
    texel = mix(texel, adjusted_color, adjust);
    frag_color = texel;
}