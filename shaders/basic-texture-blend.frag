#version 330 core

uniform sampler2D image;
uniform sampler2D prev_pass_tex;

in vec2 out_uv;

out vec4 frag_color;

void main() {
    vec4 blur_color = texture(prev_pass_tex, out_uv);
    vec4 image_color = texture(image, out_uv) * 1.25;
    frag_color = image_color + blur_color;
}