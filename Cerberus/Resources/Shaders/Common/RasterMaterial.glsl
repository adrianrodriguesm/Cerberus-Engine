#ifndef CERBERUS_RASTER_MATERIAL_GLSL
#define CERBERUS_RASTER_MATERIAL_GLSL
#extension GL_EXT_nonuniform_qualifier : require
const uint InvalidMaterialTextureIndex = 0xFFFFFFFFu;
struct GPUMaterialData
{
    vec4 AlbedoColor;
    vec4 Parameters;
    uvec4 TextureIndices;
    uvec4 TextureIndices2; // x emissive, y opacity (linear red channel)
    vec4 Flags;
    vec4 AlphaParameters; // cutoff, masked, two-sided, reserved
};
layout(std430, set = 0, binding = 1) readonly buffer MaterialTable
{
    GPUMaterialData Materials[];
} MaterialBuffer;
layout(set = 0, binding = 2) uniform sampler2D MaterialTextures[];

bool IsValidTextureIndex(uint index) { return index != InvalidMaterialTextureIndex; }

vec4 SampleMaterialTexture(uint index, vec2 uv)
{
    return texture(MaterialTextures[nonuniformEXT(index)], vec2(uv.x, 1.0 - uv.y));
}

vec4 SampleMaterialAlbedo(GPUMaterialData material, vec2 uv)
{
    vec4 albedo = material.AlbedoColor;
    albedo = IsValidTextureIndex(material.TextureIndices.x) ? albedo * SampleMaterialTexture(material.TextureIndices.x, uv) : albedo;
    return albedo;
}
void AlphaTest(GPUMaterialData material, vec2 uv, float alpha)
{
    if (IsValidTextureIndex(material.TextureIndices2.y))
        alpha *= SampleMaterialTexture(material.TextureIndices2.y, uv).r;
    if (alpha < material.AlphaParameters.x)
        discard;
}
void AlphaTest(GPUMaterialData material, vec2 uv)
{
    AlphaTest(material, uv, SampleMaterialAlbedo(material, uv).a);
}
#endif
