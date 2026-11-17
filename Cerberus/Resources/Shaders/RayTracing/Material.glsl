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

    // x = emissive texture index, y = opacity texture index, z/w reserved.
    uvec4 TextureIndices2;

    // x = packed metal-roughness (TextureIndices.z/.w share one texture; read
    // its G as roughness and B as metalness instead of each map's own R),
    // y = reconstruct normal Z from XY (BC5/2-channel normal maps).
    vec4 Flags;
    vec4 AlphaParameters;
};

layout(std430, set = 0, binding = 3) readonly buffer MaterialBuffer
{
    GPUMaterialData Materials[];
} MaterialTable;

#endif // CERBERUS_RT_MATERIAL_GLSL
