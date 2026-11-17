#version 450
#include "RasterMaterial.glsl"

layout(location = 0) in vec2 v_TexCoord;
layout(location = 1) flat in uint v_MaterialIndex;

void main()
{
    AlphaTest(MaterialBuffer.Materials[v_MaterialIndex], v_TexCoord);
}
