#ifndef CERBERUS_RT_MATERIAL_GLSL
#define CERBERUS_RT_MATERIAL_GLSL

struct GPUMaterialData
{
    vec4 AlbedoColor;

    // x = Roughness
    // y = Metalness
    // z = Emission
    // w = UseNormalMap
    vec4 Parameters;

    // x = AlbedoTextureIndex
    // y = NormalTextureIndex
    // z = MetalnessTextureIndex
    // w = RoughnessTextureIndex
    uvec4 TextureIndices;
};

layout(std430, set = 0, binding = 3) readonly buffer MaterialBuffer
{
    GPUMaterialData Materials[];
} MaterialTable;

#endif // CERBERUS_RT_MATERIAL_GLSL
