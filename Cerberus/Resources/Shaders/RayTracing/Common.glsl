#ifndef CERBERUS_RT_COMMON_GLSL
#define CERBERUS_RT_COMMON_GLSL

#extension GL_EXT_ray_tracing : require
#extension GL_EXT_nonuniform_qualifier : enable

layout(set = 0, binding = 1) uniform accelerationStructureEXT TLAS;

// Raw per-frame linear radiance from the primary path. Cross-frame accumulation
// and tone mapping are compute passes now (ReferenceAccumulate + Finalize), so
// both the reference progressive mean and the denoiser consume this same buffer.
layout(rgba32f, set = 0, binding = 14) writeonly uniform image2D RTRadiance;

// Primary-hit G-buffer for the spatiotemporal denoiser. Written by raygen on the
// first path segment only; indirect bounce logic is untouched.
//   RTGBufferNormalDepth    .xyz = world-space shading normal, .w = linear view-space depth
//   RTGBufferPositionMeshID .xyz = world-space hit position,   .w = uintBitsToFloat(instanceCustomIndex)
// On a primary miss every channel is zero and the mesh id is 0xFFFFFFFF.
layout(rgba32f, set = 0, binding = 10) writeonly uniform image2D RTGBufferNormalDepth;
layout(rgba32f, set = 0, binding = 11) writeonly uniform image2D RTGBufferPositionMeshID;

// Screen-space motion vector of the primary hit: prevUV - currUV, for the
// denoiser's temporal reprojection. Zero on a primary miss. v1 tracks camera
// motion only; animated/skinned instances are a known limitation.
layout(rg16f, set = 0, binding = 12) writeonly uniform image2D RTMotionVector;

// Primary-hit albedo, so a later step can filter illumination (radiance divided
// by albedo) and re-modulate the texture detail after the spatial filter.
layout(rgba16f, set = 0, binding = 13) writeonly uniform image2D RTGBufferAlbedo;

// Primary-hit material params: .x = roughness, .y = metalness. Lets the denoiser
// filter mirror-like specular pixels far more gently than diffuse ones.
layout(rg16f, set = 0, binding = 15) writeonly uniform image2D RTGBufferMaterial;

// Diffuse/specular split for the denoiser (Q2RTX A-SVGF / NRD style). The raygen
// separates the primary path's contribution into a diffuse and a specular part
// and DEMODULATES each -- diffuse by the diffuse albedo, specular by the
// preintegrated environment BRDF -- so the filter operates on signals free of
// texture and BRDF variation. Emissive/sky is view-independent and is kept in
// its own channel, never filtered. Finalize recombines:
//   lit = diffuse * diffuseAlbedo + specular * envBRDF + emissive
// RTRadiance (binding 14) still carries the un-split sum for reference mode.
layout(rgba32f, set = 0, binding = 16) writeonly uniform image2D RTRadianceDiffuse;
layout(rgba32f, set = 0, binding = 17) writeonly uniform image2D RTRadianceSpecular;
layout(rgba32f, set = 0, binding = 18) writeonly uniform image2D RTRadianceEmissive;

// The (clamped) preintegrated environment BRDF the raygen divided the specular
// radiance by -- specular's "albedo". Stored so Finalize re-multiplies by the
// exact same value (it cannot recompute the grazing term without the camera and
// the shading normal), making the demodulate/remodulate round trip exact.
layout(rgba16f, set = 0, binding = 19) writeonly uniform image2D RTGBufferSpecularAlbedo;

// Hit distances for the denoiser's specular filtering (Step 8.1).
//   .x = primary hit distance   (camera -> first hit),           0 on a primary miss
//   .y = specular virtual depth (first hit -> first specular bounce hit)
//        0 when the primary lobe was not specular; CERBERUS_HITDIST_MAX when the
//        specular ray escaped to the environment.
// Both clamped to CERBERUS_HITDIST_MAX so they stay finite in RG16F.
layout(rg16f, set = 0, binding = 20) writeonly uniform image2D RTGBufferHitDistance;

// Virtual specular reprojection (NRD ReLAX style). The raygen reconstructs the
// virtual reflection point (primary hit + mirror dir * reflected-ray distance),
// projects it with last frame's view-projection, and blends that motion vector
// with the surface one by roughness -> RTMotionVectorSpecular. RTSpecReflHistory
// carries what SVGFTemporalSplit validates the virtual reprojection against
// (NOT the surface G-buffer): .x = virtual point view-space Z, .y = reflected-ray
// hit distance. Surface motion / zero for pixels that are not specular enough.
layout(rg16f, set = 0, binding = 21) writeonly uniform image2D RTMotionVectorSpecular;
layout(rg16f, set = 0, binding = 22) writeonly uniform image2D RTSpecReflHistory;

// Direct-lighting luminance of the primary hit (NEE result at bounce 0, before
// indirect bounces). Deterministic for a static scene, so SVGFTemporalSplit can
// build a true A-SVGF temporal gradient from it -- comparing this pixel's value
// to last frame's at the reprojected world point -- instead of the ring-mean
// heuristic that camera motion fools. Zero relative change => keep history even
// while panning; a real lighting/shadow change => shorten it.
layout(r32f, set = 0, binding = 23) writeonly uniform image2D RTDirectLuma;

const float CERBERUS_RAY_T_MIN = 0.001;
const float CERBERUS_RAY_T_MAX = 100000.0;
const float CERBERUS_HITDIST_MAX = 60000.0;

#endif // CERBERUS_RT_COMMON_GLSL
