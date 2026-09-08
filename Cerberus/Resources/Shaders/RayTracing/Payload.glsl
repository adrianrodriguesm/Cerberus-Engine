#ifndef CERBERUS_RT_PAYLOAD_GLSL
#define CERBERUS_RT_PAYLOAD_GLSL

struct HitPayload
{
    vec3 WorldPosition;
    float HitT;

    vec3 ShadingNormal;
    float Roughness;

    vec3 GeometricNormal;
    float Metalness;

    vec3 Albedo;
    uint MaterialIndex;

    vec3 Emission;
    uint Hit;

    vec2 TexCoord;
    uint InstanceIndex;
    uint Padding;
};

#endif // CERBERUS_RT_PAYLOAD_GLSL
