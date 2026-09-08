#type compute
#version 450 core

// Spatial pre-resolve: a small, FIXED-radius edge-aware bilateral over the raw
// 1-spp demodulated diffuse/specular radiance, run BEFORE SVGFTemporalSplit
// (MegaLights ShadeLightSamplesCS -> DenoiserSpatialCS -> DenoiserTemporalCS
// order; NRD/ReLAX pre-blur before temporal accumulation).
//
// Why: every attempt at a MegaLights-style confidence/variance mechanism in the
// temporal and a-trous passes fed on RAW 1-spp neighbourhood statistics, which
// are noisy and biased (see SVGFTemporalSplit.glsl / SVGFAtrousSplit.glsl -- a
// neighbourhood clamp driven by a noisy mean darkens converged highlights, a
// disocclusion factor driven by a noisy variance over- or under-fires). This
// pass gives the rest of the chain a less-raw signal to work with, the same way
// MegaLights' DenoiserSpatialCS runs before its DenoiserTemporalCS.
//
// Deliberately NOT the a-trous: one pass, fixed 3x3, no history/variance
// awareness. This is a pre-filter, not the denoiser -- the temporal EMA and the
// split a-trous still do the real work. In/out are separate images (not
// in-place): a spatial gather cannot safely read neighbours of an image the same
// dispatch is also writing.

layout(local_size_x = 16, local_size_y = 16, local_size_z = 1) in;

layout(binding = 0, rgba32f) readonly  uniform image2D u_InRadianceDiffuse;
layout(binding = 1, rgba32f) readonly  uniform image2D u_InRadianceSpecular;
layout(binding = 2, rgba32f) readonly  uniform image2D u_NormalDepth;    // .xyz world normal, .w linear view depth
layout(binding = 3, rgba32f) readonly  uniform image2D u_PositionMeshID; // .xyz world pos, .w uintBitsToFloat(meshID)
layout(binding = 4, rgba32f) writeonly uniform image2D u_OutRadianceDiffuse;
layout(binding = 5, rgba32f) writeonly uniform image2D u_OutRadianceSpecular;

layout(push_constant) uniform Push
{
    // 0 => passthrough (A/B baseline: SVGFTemporalSplit then accumulates the raw
    // 1-spp signal directly, exactly as before this pass existed).
    uint Enabled;
} u_Push;

const uint  CERBERUS_SKY_MESH_ID = 0xFFFFFFFFu;
const float CERBERUS_SIGMA_NORMAL = 32.0;
// World-space plane tolerance, in units of one pixel's world-space size on this
// surface (same convention as SVGFTemporalSplit's reprojection plane test) --
// camera-motion-invariant, unlike a raw depth difference.
const float CERBERUS_PLANE_TOLERANCE = 4.0;
// Illumination edge stop: do not blend a lit tap into a shadowed centre (or vice
// versa) on the same flat mesh. Same shape as SVGFHistoryFix's edge stop.
const float CERBERUS_LUMA_SIGMA = 0.6;
// MegaLights-style filter gate (DenoiserSpatialCS: bFilterDiffuse = relative
// std-dev > 0.2/0.5): only blend in the bilateral result where the local
// relative luma std-dev actually indicates noise (a shadow / GI corner). Below
// LOW the pixel is returned untouched -- a constant per-frame blur on every
// pixel, even a clean well-lit one, is a systematic low-pass baked into the
// temporal EMA (not noise that averages out), which is what produced a
// permanently hazier image under motion.
const float CERBERUS_REL_STDDEV_LOW  = 0.15;
const float CERBERUS_REL_STDDEV_HIGH = 0.6;

float Luminance(vec3 c) { return dot(c, vec3(0.2126, 0.7152, 0.0722)); }

// channel: 0 diffuse, 1 specular (GLSL forbids passing an image as a parameter
// with its format qualifier -- same convention as SVGFTemporalSplit.glsl).
vec3 Resolve(int channel, ivec2 pixel, ivec2 size, vec3 normal, vec3 worldPos,
    uint meshID, float posGradient)
{
    vec3  centerColor = channel == 0 ? imageLoad(u_InRadianceDiffuse, pixel).rgb
                                     : imageLoad(u_InRadianceSpecular, pixel).rgb;
    float centerLuma  = Luminance(centerColor);

    vec3  sumColor  = centerColor;
    float sumWeight = 1.0;

    // Unweighted-by-luma spatial stats (so the very edge we are characterising
    // does not suppress itself out of the estimate) -- used only to decide HOW
    // MUCH to filter, not what to filter.
    float sumLuma   = centerLuma;
    float sumLumaSq = centerLuma * centerLuma;
    float statCount = 1.0;

    for (int dy = -1; dy <= 1; ++dy)
    for (int dx = -1; dx <= 1; ++dx)
    {
        if (dx == 0 && dy == 0)
            continue;
        ivec2 tap = pixel + ivec2(dx, dy);
        if (tap.x < 0 || tap.y < 0 || tap.x >= size.x || tap.y >= size.y)
            continue;

        vec4 tapPosMesh = imageLoad(u_PositionMeshID, tap);
        if (floatBitsToUint(tapPosMesh.w) != meshID)
            continue;

        vec4 tapND = imageLoad(u_NormalDepth, tap);
        float normalWeight = pow(max(dot(normal, tapND.xyz), 0.0), CERBERUS_SIGMA_NORMAL);

        float planeDist = abs(dot(tapPosMesh.xyz - worldPos, normal));
        if (planeDist > CERBERUS_PLANE_TOLERANCE * posGradient)
            continue;

        vec3  tapColor = channel == 0 ? imageLoad(u_InRadianceDiffuse, tap).rgb
                                       : imageLoad(u_InRadianceSpecular, tap).rgb;
        float tapLuma  = Luminance(tapColor);

        sumLuma   += tapLuma;
        sumLumaSq += tapLuma * tapLuma;
        statCount += 1.0;

        float lumaWeight = exp(-abs(centerLuma - tapLuma) /
            (CERBERUS_LUMA_SIGMA * max(centerLuma, tapLuma) + 0.05));

        float w = normalWeight * lumaWeight;
        sumColor  += w * tapColor;
        sumWeight += w;
    }

    vec3  bilateral = sumColor / sumWeight;

    float meanLuma  = sumLuma / statCount;
    float varLuma   = max(0.0, sumLumaSq / statCount - meanLuma * meanLuma);
    float relStdDev = sqrt(varLuma) / (meanLuma + 0.05);
    float filterStrength = smoothstep(CERBERUS_REL_STDDEV_LOW, CERBERUS_REL_STDDEV_HIGH, relStdDev);

    return mix(centerColor, bilateral, filterStrength);
}

void main()
{
    ivec2 pixel = ivec2(gl_GlobalInvocationID.xy);
    ivec2 size  = imageSize(u_NormalDepth);
    if (pixel.x >= size.x || pixel.y >= size.y)
        return;

    if (u_Push.Enabled == 0u)
    {
        imageStore(u_OutRadianceDiffuse, pixel, imageLoad(u_InRadianceDiffuse, pixel));
        imageStore(u_OutRadianceSpecular, pixel, imageLoad(u_InRadianceSpecular, pixel));
        return;
    }

    vec4 posMesh = imageLoad(u_PositionMeshID, pixel);
    uint meshID  = floatBitsToUint(posMesh.w);

    if (meshID == CERBERUS_SKY_MESH_ID)
    {
        imageStore(u_OutRadianceDiffuse, pixel, imageLoad(u_InRadianceDiffuse, pixel));
        imageStore(u_OutRadianceSpecular, pixel, imageLoad(u_InRadianceSpecular, pixel));
        return;
    }

    vec3 normal   = imageLoad(u_NormalDepth, pixel).xyz;
    vec3 worldPos = posMesh.xyz;

    // World-space size of ~one pixel on this surface (same convention as
    // SVGFTemporalSplit's reprojection plane test).
    float posGradient = 1e-4;
    for (int dy = -1; dy <= 1; ++dy)
    for (int dx = -1; dx <= 1; ++dx)
    {
        if (dx == 0 && dy == 0)
            continue;
        ivec2 t = pixel + ivec2(dx, dy);
        if (t.x < 0 || t.y < 0 || t.x >= size.x || t.y >= size.y)
            continue;
        vec4 tp = imageLoad(u_PositionMeshID, t);
        if (floatBitsToUint(tp.w) != meshID)
            continue;
        posGradient = max(posGradient, length(worldPos - tp.xyz) / max(float(abs(dx) + abs(dy)), 1.0));
    }

    vec3 resolvedDiffuse  = Resolve(0, pixel, size, normal, worldPos, meshID, posGradient);
    vec3 resolvedSpecular = Resolve(1, pixel, size, normal, worldPos, meshID, posGradient);

    imageStore(u_OutRadianceDiffuse, pixel, vec4(resolvedDiffuse, 0.0));
    imageStore(u_OutRadianceSpecular, pixel, vec4(resolvedSpecular, 0.0));
}
