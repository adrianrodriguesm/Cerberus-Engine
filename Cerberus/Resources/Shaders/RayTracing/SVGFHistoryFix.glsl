#type compute
#version 450 core

// History-fix pass (NRD ReLAX "HistoryFix"), between SVGFTemporalSplit and the
// split a-trous. For a converged pixel it is a pure passthrough. For a pixel
// whose temporal history is still short -- a freshly disoccluded pixel, or the
// moving leading-edge band while the camera pans -- it replaces the noisy 1-spp
// temporal result with a wide bilateral gather of the ACCUMULATED history of its
// neighbours, weighted by their own history length. A young pixel next to a
// converged region thus inherits a near-clean estimate instead of raw noise,
// which is what the split a-trous alone (4 x 5x5) cannot hide.
//
// Static-camera safety: every pixel reaches historyLength = MAX within a handful
// of frames, so on a still camera this pass is a passthrough everywhere and the
// image is bit-identical to running without it.
//
// In place: reads the temporal result from the interim colour-history copy
// (u_InColor*, which SVGFTemporalSplit wrote identical to the temporal colour)
// and writes the fixed result back into u_OutTemporalColor* -- no extra buffer,
// no read/write hazard on the same image.

layout(local_size_x = 16, local_size_y = 16, local_size_z = 1) in;

layout(binding = 0, rgba16f) writeonly uniform image2D u_OutTemporalColorDiff; // .rgb illum, .a variance
layout(binding = 1, rgba16f) writeonly uniform image2D u_OutTemporalColorSpec;
layout(binding = 2, rgba16f) readonly  uniform image2D u_InColorDiff;          // temporal result snapshot
layout(binding = 3, rgba16f) readonly  uniform image2D u_InColorSpec;
layout(binding = 4, rgba32f) readonly  uniform image2D u_MomentsDiff;          // .z history length
layout(binding = 5, rgba32f) readonly  uniform image2D u_MomentsSpec;
layout(binding = 6, rgba32f) readonly  uniform image2D u_NormalDepth;          // .xyz world normal, .w linear view Z
layout(binding = 7, rgba32f) readonly  uniform image2D u_PositionMeshID;       // .w uintBitsToFloat(meshID)

const uint  CERBERUS_SKY_MESH_ID = 0xFFFFFFFFu;
// History length at/above which the pass is a passthrough. Below it, the spatial
// fill ramps in (fully at historyLength = 1).
const float CERBERUS_HISTORY_FIX_THRESHOLD = 16.0;
// Widest half-extent (pixels) of the strided bilateral gather, for the youngest
// pixels. The gather is always ~9x9 taps -- the stride grows with the radius.
// Wide enough that the frame-edge band under a fast pan can still reach the
// history-bearing interior.
const float CERBERUS_HISTORY_FIX_MAX_RADIUS = 16.0;
const float CERBERUS_HISTORY_FIX_SIGMA_NORMAL = 32.0;

float HFLuminance(vec3 c) { return dot(c, vec3(0.2126, 0.7152, 0.0722)); }

// Fill one channel. channel: 0 diffuse, 1 specular.
void FixChannel(int channel, ivec2 pixel, ivec2 size, vec3 normal, float linZ,
    uint meshID, float depthGradient, out vec4 result)
{
    vec4  center  = channel == 0 ? imageLoad(u_InColorDiff, pixel)
                                 : imageLoad(u_InColorSpec, pixel);
    float histLen = (channel == 0 ? imageLoad(u_MomentsDiff, pixel)
                                  : imageLoad(u_MomentsSpec, pixel)).z;

    // Converged -> passthrough (the common case; static camera hits this always).
    if (histLen >= CERBERUS_HISTORY_FIX_THRESHOLD)
    {
        result = center;
        return;
    }

    float youth  = 1.0 - clamp(histLen / CERBERUS_HISTORY_FIX_THRESHOLD, 0.0, 1.0);
    float radius  = mix(2.0, CERBERUS_HISTORY_FIX_MAX_RADIUS, youth);
    int   stride  = max(1, int(radius) / 4);
    float invS2   = 1.0 / (2.0 * radius * radius);

    // Illumination edge stop: a lit surface next to a shadowed part of the SAME
    // mesh (same normal, same plane) must NOT bleed the shadow into the light.
    // Uses the CONVERGED neighbour history luma so it is not fooled by 1-spp
    // noise -- a young centre still gets a real luminance reference from its
    // converged neighbours.
    float centerLuma = HFLuminance(center.rgb);

    vec3  sumColor = vec3(0.0);
    float sumVar   = 0.0;
    float sumW     = 0.0;

    for (int sy = -4; sy <= 4; ++sy)
    for (int sx = -4; sx <= 4; ++sx)
    {
        ivec2 off = ivec2(sx, sy) * stride;
        ivec2 t   = pixel + off;
        if (t.x < 0 || t.y < 0 || t.x >= size.x || t.y >= size.y)
            continue;
        if (floatBitsToUint(imageLoad(u_PositionMeshID, t).w) != meshID)
            continue;

        vec4  tND = imageLoad(u_NormalDepth, t);
        float nw  = pow(max(dot(normal, tND.xyz), 0.0), CERBERUS_HISTORY_FIX_SIGMA_NORMAL);
        float dw  = exp(-abs(linZ - tND.w) /
            (depthGradient * length(vec2(off)) + 1e-3));
        float sw  = exp(-dot(vec2(off), vec2(off)) * invS2);

        vec4  tc    = channel == 0 ? imageLoad(u_InColorDiff, t)
                                   : imageLoad(u_InColorSpec, t);
        float tHist = (channel == 0 ? imageLoad(u_MomentsDiff, t)
                                    : imageLoad(u_MomentsSpec, t)).z;
        // Pull mainly from neighbours that actually HAVE accumulated history.
        float hw = clamp(tHist / CERBERUS_HISTORY_FIX_THRESHOLD, 0.03, 1.0);
        // Illumination edge stop (see above).
        float lw = exp(-abs(centerLuma - HFLuminance(tc.rgb)) /
            (0.6 * max(centerLuma, HFLuminance(tc.rgb)) + 0.05));

        float w = nw * dw * sw * hw * lw;
        sumColor += w * tc.rgb;
        sumVar   += w * tc.a;
        sumW     += w;
    }

    if (sumW < 1e-5)
    {
        result = center;
        return;
    }

    vec3  spatial    = sumColor / sumW;
    float spatialVar = sumVar   / sumW;
    // Young pixel -> mostly the spatial fill; closer to the threshold -> mostly
    // its own accumulation. Variance is raised for young pixels so the a-trous
    // still treats them as uncertain.
    result.rgb = mix(center.rgb, spatial, youth);
    result.a   = max(center.a, spatialVar);
}

void main()
{
    ivec2 pixel = ivec2(gl_GlobalInvocationID.xy);
    ivec2 size  = imageSize(u_NormalDepth);
    if (pixel.x >= size.x || pixel.y >= size.y)
        return;

    vec4  nd     = imageLoad(u_NormalDepth, pixel);
    vec3  normal = nd.xyz;
    float linZ   = nd.w;
    uint  meshID = floatBitsToUint(imageLoad(u_PositionMeshID, pixel).w);

    // Sky: the temporal pass already wrote the correct value into
    // u_OutTemporalColor*; leave it untouched. (u_InColor* -- the ColorHistory
    // copy -- is NOT written for sky pixels, so reading it here would smear stale
    // history over the sky under camera motion.)
    if (meshID == CERBERUS_SKY_MESH_ID)
        return;

    float depthGradient = 1e-3;
    if (pixel.x + 1 < size.x) depthGradient = max(depthGradient, abs(linZ - imageLoad(u_NormalDepth, pixel + ivec2(1, 0)).w));
    if (pixel.x - 1 >= 0)     depthGradient = max(depthGradient, abs(linZ - imageLoad(u_NormalDepth, pixel - ivec2(1, 0)).w));
    if (pixel.y + 1 < size.y) depthGradient = max(depthGradient, abs(linZ - imageLoad(u_NormalDepth, pixel + ivec2(0, 1)).w));
    if (pixel.y - 1 >= 0)     depthGradient = max(depthGradient, abs(linZ - imageLoad(u_NormalDepth, pixel - ivec2(0, 1)).w));

    vec4 outD, outS;
    FixChannel(0, pixel, size, normal, linZ, meshID, depthGradient, outD);
    FixChannel(1, pixel, size, normal, linZ, meshID, depthGradient, outS);

    imageStore(u_OutTemporalColorDiff, pixel, outD);
    imageStore(u_OutTemporalColorSpec, pixel, outS);
}
