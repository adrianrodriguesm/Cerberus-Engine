#ifndef CERBERUS_RT_TEXTURE_TABLE_GLSL
#define CERBERUS_RT_TEXTURE_TABLE_GLSL

const uint CERBERUS_INVALID_TEXTURE_INDEX = 0xFFFFFFFFu;

layout(set = 0, binding = 5) uniform sampler2D RTTextures[];

bool IsValidRTTextureIndex(uint textureIndex)
{
	return textureIndex != CERBERUS_INVALID_TEXTURE_INDEX;
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
