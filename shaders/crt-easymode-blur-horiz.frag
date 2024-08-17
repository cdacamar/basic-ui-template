#version 330 core

uniform sampler2D image;
uniform vec2 resolution;
uniform float custom_float_value1;
uniform float custom_float_value2;

in vec2 out_uv;

out vec4 frag_color;

// Higher value, more centered glow.
// Lower values might need more taps.
#define GLOW_FALLOFF custom_float_value1
#define TAPS int(custom_float_value2)

#define kernel(x) exp(-GLOW_FALLOFF * (x) * (x))

vec4 blur_horiz(vec2 tex, sampler2D s0, vec2 texture_size)
{
    vec4 col = vec4(0.0);
    float dx = 1.0 / texture_size.x;

    float k_total = 0.0;
    for (int i = -TAPS; i <= TAPS; i++)
    {
        float k = kernel(i);
        k_total += k;
        col += k * texture(s0, tex + vec2(float(i) * dx, 0.0));
    }
    return vec4(col / k_total);
}

void main()
{
    frag_color = blur_horiz(out_uv, image, resolution);
}