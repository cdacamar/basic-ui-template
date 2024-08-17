#version 330 core

uniform float time;

in vec4 out_color;
in vec2 out_uv;

uniform float weight[5] = float[] (0.227027, 0.1945946, 0.1216216, 0.054054, 0.016216);

out vec4 frag_color;

void main() {
    vec4 base_color = out_color;
    vec4 mix_color = base_color;
    mix_color.a = 0;
    vec4 result = base_color * weight[0];
    for(int i = 1; i < 5; ++i)
    {
#if 0 // Weird gradient
        result += mix(base_color, mix_color, (out_uv.x + out_uv.y - i * out_uv.x * out_uv.y)) * weight[i];
        result += mix(base_color, mix_color, (out_uv.x + out_uv.y + i * out_uv.x * out_uv.y)) * weight[i];
#endif

#if 0 // X-gradient left (most intense) to right (less intense).
        result += mix(base_color, mix_color, (out_uv.x - i * out_uv.x)) * weight[i];
        result += mix(base_color, mix_color, (out_uv.x + i * out_uv.x)) * weight[i];
#endif

#if 1 // Best result so far!
        result += mix(base_color, mix_color, -(i * (out_uv.y - 0.75))) * weight[i];
        result += mix(base_color, mix_color, -(i * (out_uv.y - 0.75))) * weight[i];
#endif
    }
    frag_color = result;
}