#type compute
#version 450 core
// Physically Based Rendering
// Copyright (c) 2017-2018 Michał Siejak
// Converts equirectangular (lat-long) projection texture into a cubemap

const float PI = 3.141592;

layout(binding = 0, rgba32f) restrict writeonly uniform imageCube o_CubeMap;
layout(binding = 1) uniform sampler2D u_EquirectangularTex;

// Calculate normalized sampling direction vector based on current fragment coordinates (gl_GlobalInvocationID.xyz).
// This is essentially "inverse-sampling": we reconstruct what the sampling vector would be if we wanted it to "hit"
// this particular fragment in a cubemap.
// See: OpenGL core profile specs, section 8.13.
vec3 GetCubeMapTexCoord()
{
    // uv and st are both essentially "x" and "y" but for texture coordinates.
    // This function works out which face/pixel of the cubemap we want to write
    // to from the current invocation of the compute shader - we then use these
    // coordinates to work out where we should be sampling from in the input
    // equirectangular image to find the color value we should write to the cubemap
    vec2 st = gl_GlobalInvocationID.xy / vec2(imageSize(o_CubeMap));
    // Map to Range [-1,1]
    vec2 uv = 2.0 * vec2(st.x, st.y) - vec2(1.0);

    vec3 ret;
    if (gl_GlobalInvocationID.z == 0)      ret = vec3(  1.0, -uv.y, -uv.x); // Right
    else if (gl_GlobalInvocationID.z == 1) ret = vec3( -1.0, -uv.y,  uv.x); // Left
    else if (gl_GlobalInvocationID.z == 2) ret = vec3( uv.x,  1.0, uv.y);   // Top
    else if (gl_GlobalInvocationID.z == 3) ret = vec3( uv.x, -1.0,  -uv.y); // Bottom
    else if (gl_GlobalInvocationID.z == 4) ret = vec3( uv.x, -uv.y,   1.0); // Front
    else if (gl_GlobalInvocationID.z == 5) ret = vec3(-uv.x, -uv.y,  -1.0); // Back
    return normalize(ret);
}

layout(local_size_x = 32, local_size_y = 32, local_size_z = 1) in;
void main()
{
	vec3 cubeTC = GetCubeMapTexCoord();

    // Convert Cartesian direction vector to spherical coordinates.
	// https://en.wikipedia.org/wiki/Spherical_coordinate_system#Cartesian_coordinates
	float phi = atan(cubeTC.z, cubeTC.x);
	float theta = acos(cubeTC.y);
    vec2 uv = vec2(phi / (2.0 * PI) + 0.5, theta / PI);

	vec4 color = texture(u_EquirectangularTex, uv);
	imageStore(o_CubeMap, ivec3(gl_GlobalInvocationID), color);
}
