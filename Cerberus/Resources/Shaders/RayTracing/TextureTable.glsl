#ifndef CERBERUS_RT_TEXTURE_TABLE_GLSL
#define CERBERUS_RT_TEXTURE_TABLE_GLSL

// Must match cerberus::MaxRayTracingTextureCount.
const uint CERBERUS_RT_MAX_TEXTURES = 128u;

layout(set = 0, binding = 5) uniform sampler2D RTTextures[128];

bool IsValidRTTextureIndex(uint textureIndex)
{
    return textureIndex < CERBERUS_RT_MAX_TEXTURES;
}

vec4 SampleRTTexture(uint textureIndex, vec2 texCoord)
{
    // Ray-tracing stages have no implicit screen-space derivatives. LOD 0 is
    // intentional for the first texture phase; ray cones can select mips later.
    // Assimp UVs use a bottom-left V origin while stb_image keeps the first file
    // row at the top. Flip only for RT sampling so mesh data stays canonical.
    vec2 samplingUV = vec2(texCoord.x, 1.0 - texCoord.y);
    return textureLod(RTTextures[nonuniformEXT(textureIndex)], samplingUV, 0.0);
}

#endif // CERBERUS_RT_TEXTURE_TABLE_GLSL
