#version 330 core

uniform sampler2D image;
uniform vec2 resolution;

in vec4 out_color;
in vec2 out_uv;

out vec4 frag_color;

// Borrowed from: https://stackoverflow.com/questions/1506299/applying-brightness-and-contrast-with-opengl-es
vec4 adjust_brightness(vec4 color) {
    float bright = 1.25;
    vec4 luminance = vec4(1.0);
    float contrast = 1.0;
    return mix(color * bright, mix(luminance, color, contrast), 0.5);
}

void main() {
    float texel = texture(image, out_uv).r;
    vec4 color = vec4(out_color.rgb, texel * out_color.a);
    // Brighten the final result a bit for a more readable text.
    frag_color = adjust_brightness(color);
}