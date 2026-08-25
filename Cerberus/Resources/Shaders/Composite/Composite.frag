#version 450

layout(set = 0, binding = 0) uniform sampler2D SceneColor;

layout(push_constant) uniform u_Composite
{
    float Exposure;
} Composite;

layout(location = 0) in vec2 v_TexCoord;
layout(location = 0) out vec4 o_Color;

vec3 ACESFilm(vec3 color)
{
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return clamp((color * (a * color + b)) / (color * (c * color + d) + e), 0.0, 1.0);
}

vec3 GammaCorrect(vec3 color)
{
    return pow(color, vec3(1.0 / 2.2));
}

void main()
{
    vec3 hdrColor = texture(SceneColor, v_TexCoord).rgb;
    vec3 tonemapped = ACESFilm(hdrColor * max(Composite.Exposure, 0.0));
    o_Color = vec4(GammaCorrect(tonemapped), 1.0);
}
