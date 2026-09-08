#type compute
#version 450 core

// SVGF temporal accumulation, diffuse + specular in one dispatch (Q2RTX A-SVGF /
// NRD style). The raygen already demodulated both signals -- diffuse by the
// diffuse albedo, specular by the preintegrated env BRDF -- so this pass just
// reprojects and blends two BRDF-free illumination signals. Emissive is
// view-independent and never enters here.
//
// Reprojection:
//   - diffuse : surface motion vector -> bilinear taps -> mesh/normal/depth
//               consistency -> nearest-tap fallback.
//   - specular: along the VIRTUAL reflection motion vector (u_MotionVectorSpecular,
//               roughness-blended surface<->virtual by the raygen). Each tap is
//               validated against the REFLECTION geometry -- the reprojected
//               virtual view-Z and reflected-ray hit distance must match
//               (u_SpecReflPrev), NOT the surface G-buffer. If nothing validates,
//               fall back to the plain surface reprojection (never a bare reset).
//
// Blend (both channels), reference form:
//   - alpha = max(AlphaMin, 1/historyLength, gradient)   (A-SVGF adaptive alpha)
//   - the reprojected history is clamped to the current 3x3 colour AABB before
//     the blend (TAA / ReLAX neighbourhood clamp) -- history that would ghost is
//     pulled into the local colour range instead of forcing a history reset.
//   - the A-SVGF temporal gradient (u_DirectLuma*, camera-motion-invariant) is
//     the ONLY thing that shortens history on a lighting change; camera motion
//     alone, with a valid reprojection, keeps a full-length history.

layout(local_size_x = 16, local_size_y = 16, local_size_z = 1) in;

layout(binding = 0,  rgba32f) readonly  uniform image2D u_RadianceDiffuse;     // demodulated diffuse illumination, 1 spp
layout(binding = 1,  rgba32f) readonly  uniform image2D u_RadianceSpecular;    // demodulated specular illumination, 1 spp
layout(binding = 2,  rgba32f) readonly  uniform image2D u_CurrNormalDepth;     // .xyz world normal, .w linear view depth
layout(binding = 3,  rgba32f) readonly  uniform image2D u_CurrPositionMeshID;  // .w uintBitsToFloat(meshID)
layout(binding = 4,  rg16f)   readonly  uniform image2D u_MotionVector;         // .xy = prevUV - currUV (surface)
layout(binding = 5,  rg16f)   readonly  uniform image2D u_MotionVectorSpecular; // .xy = prevUV - currUV (virtual reflection point, roughness-blended)
layout(binding = 6,  rgba16f) readonly  uniform image2D u_PrevColorHistoryDiff;// .rgb once-filtered illumination
layout(binding = 7,  rgba32f) readonly  uniform image2D u_PrevMomentsDiff;     // .x E[l], .y E[l^2], .z history length (32F: E[l^2] range)
layout(binding = 8,  rgba16f) readonly  uniform image2D u_PrevColorHistorySpec;
layout(binding = 9,  rgba32f) readonly  uniform image2D u_PrevMomentsSpec;
layout(binding = 10, rgba32f) readonly  uniform image2D u_PrevHistoryGBuffer;  // .xy octNormal, .z linZ, .w meshID (shared)
layout(binding = 11, rgba16f) writeonly uniform image2D u_OutTemporalColorDiff;// .rgb illumination EMA, .a variance
layout(binding = 12, rgba32f) writeonly uniform image2D u_OutMomentsDiff;      // .x E[l], .y E[l^2], .z history length, .w applied alpha
layout(binding = 13, rgba16f) writeonly uniform image2D u_OutTemporalColorSpec;
layout(binding = 14, rgba32f) writeonly uniform image2D u_OutMomentsSpec;
layout(binding = 15, rgba32f) writeonly uniform image2D u_OutHistoryGBuffer;   // shared
// Reprojection source for next frame. This pass seeds it (= the raw EMA); the
// split a-trous iteration 0 then overwrites it with the filtered result
// (Schied's post-first-iteration feedback).
layout(binding = 16, rgba16f) writeonly uniform image2D u_OutColorHistoryDiff;
layout(binding = 17, rgba16f) writeonly uniform image2D u_OutColorHistorySpec;
layout(binding = 18, rg16f)   readonly  uniform image2D u_Material;         // .x roughness, .y metalness
layout(binding = 19, rg16f)   readonly  uniform image2D u_SpecReflCurr;     // this frame: .x virtual view Z, .y reflected-ray hit dist
layout(binding = 20, rg16f)   readonly  uniform image2D u_SpecReflPrev;     // previous frame (ping-pong), same layout
layout(binding = 21, rg16f)   writeonly uniform image2D u_OutSpecReflHistory;// -> ping-pong curr, = u_SpecReflCurr passthrough
layout(binding = 22, r32f)    readonly  uniform image2D u_DirectLumaCurr;   // this frame's primary-hit direct-lighting luma
layout(binding = 23, r32f)    readonly  uniform image2D u_PrevDirectLuma;   // previous frame (ping-pong)
layout(binding = 24, r32f)    writeonly uniform image2D u_OutDirectLuma;    // -> ping-pong curr passthrough

layout(push_constant) uniform Push
{
    // 0 => no valid history this frame (resize / structural scene edit).
    uint  FrameIndex;
    // A-SVGF antilag sensitivity. 0 disables. 1 reacts from ~4 sigma.
    float AntilagStrength;
    // Temporal blend floor for both channels. alpha = max(AlphaMin, 1/history,
    // antilag). ~0.05 keeps the loop self-damping while still converging close to
    // the reference on a still camera; 0 = pure progressive mean.
    float AlphaMin;
    // 0 => specular reprojects along the surface motion only (A/B baseline), no
    // reflection-geometry check. Parallax distrust + spatial-mean blend still on.
    uint  SpecVirtualReproj;
} u_Push;

const uint  CERBERUS_SKY_MESH_ID = 0xFFFFFFFFu;
// Effective-history cap: frames 1..MAX are an exact progressive mean, then a slow
// EMA holds the result (unless AlphaMin raises the floor first).
const float CERBERUS_MAX_HISTORY = 512.0;
const float CERBERUS_NORMAL_MIN_DOT = 0.9;
// Looser normal test for the virtual reprojection: the source pixel sits
// elsewhere on a curved mirror and carries a different SURFACE normal (the
// reflection check below is the real discriminator).
const float CERBERUS_NORMAL_MIN_DOT_VIRTUAL = 0.5;
// Reprojection plane test: how far (in units of the world-space size of one
// pixel) a tap's world position may sit off this pixel's tangent plane. Both
// positions are current-frame, so unlike a raw depth difference this does NOT
// fire when the camera dollies toward/away from a flat surface.
const float CERBERUS_PLANE_TOLERANCE = 4.0;
// Reflection-geometry consistency (point 4). A virtual-motion tap is kept only if
// the previous frame's virtual view-Z and reflected-ray hit distance stored there
// match this pixel's within these relative tolerances.
const float CERBERUS_VIRTUAL_Z_REL_TOL    = 0.10;
const float CERBERUS_VIRTUAL_DIST_REL_TOL = 0.30;
// Roughness above which the reflection check is skipped and the surface
// reprojection is used directly (rough reflections are ~attached to the surface).
const float CERBERUS_SPEC_VIRTUAL_ROUGHNESS_MAX = 0.45;
// A-SVGF temporal gradient epsilon (relative-change denominator floor).
const float CERBERUS_GRAD_EPS = 0.02;
// Half-extent (pixels) of the ReLAX-style reprojection search. The surface motion
// vector is only pixel-accurate, so under a fast pan the true previous sample of
// an already-visible, already-converged surface can sit a few pixels outside the
// bilinear footprint -- on a curved or detailed surface the normal test then
// rejects every bilinear tap and the pixel resets to 1-spp noise even though its
// history is right there. The search scans this window for the nearest consistent
// previous pixel before declaring a disocclusion. 2 == ReLAX default.
const int CERBERUS_REPROJ_SEARCH = 2;
// Neighbourhood variance clip (TAA / ReLAX / Lumen "FastUpdateMode" rectify):
// where the temporal gradient fires, history is clamped to the current 5x5
// colour box `mean +/- gamma * stddev` to rectify a now-stale reprojection.
const float CERBERUS_CLIP_GAMMA_FRESH = 2.5;

float Luminance(vec3 c) { return dot(c, vec3(0.2126, 0.7152, 0.0722)); }

// Octahedral normal encode/decode (Cigolle et al. 2014).
vec2 OctEncode(vec3 n)
{
    n /= (abs(n.x) + abs(n.y) + abs(n.z));
    vec2 e = n.xy;
    if (n.z < 0.0)
        e = (1.0 - abs(e.yx)) * vec2(e.x >= 0.0 ? 1.0 : -1.0, e.y >= 0.0 ? 1.0 : -1.0);
    return e;
}

vec3 OctDecode(vec2 e)
{
    vec3 n = vec3(e.xy, 1.0 - abs(e.x) - abs(e.y));
    float t = max(-n.z, 0.0);
    n.x += n.x >= 0.0 ? -t : t;
    n.y += n.y >= 0.0 ? -t : t;
    return normalize(n);
}

vec4 PackHistoryGBuffer(vec3 normal, float linearDepth, uint meshID)
{
    return vec4(OctEncode(normal), linearDepth, uintBitsToFloat(meshID));
}

// Ring statistics of one demodulated illumination signal over the 3x3
// neighbourhood (centre excluded), same-surface taps only. mean/max feed the
// firefly clamp; the spread (var) is the antilag discriminator. channel: 0
// diffuse, 1 specular (GLSL forbids passing an image as a parameter with its
// format qualifier).
void RingLuma(int channel, ivec2 pixel, ivec2 size, uint meshID,
    out float meanLuma, out float maxLuma, out float varLuma, out float count)
{
    float sum = 0.0, sumSq = 0.0;
    count = 0.0;
    maxLuma = 0.0;
    for (int dy = -1; dy <= 1; ++dy)
    for (int dx = -1; dx <= 1; ++dx)
    {
        if (dx == 0 && dy == 0)
            continue;
        ivec2 t = pixel + ivec2(dx, dy);
        if (t.x < 0 || t.y < 0 || t.x >= size.x || t.y >= size.y)
            continue;
        if (floatBitsToUint(imageLoad(u_CurrPositionMeshID, t).w) != meshID)
            continue;
        vec3 c = channel == 0
            ? imageLoad(u_RadianceDiffuse, t).rgb
            : imageLoad(u_RadianceSpecular, t).rgb;
        float l = Luminance(c);
        sum += l;
        sumSq += l * l;
        count += 1.0;
        maxLuma = max(maxLuma, l);
    }
    if (count > 0.0)
    {
        meanLuma = sum / count;
        varLuma  = max(0.0, sumSq / count - meanLuma * meanLuma);
    }
    else
    {
        meanLuma = -1.0;
        varLuma  = 0.0;
    }
}

// 5x5 same-surface colour mean and std-dev of the demodulated current signal,
// for the TAA / ReLAX variance clip. Returns the raw mean/stddev; the caller
// forms the box mean +/- gamma*stddev with a history-length-dependent gamma.
// channel: 0 diffuse, 1 specular.
void NeighborhoodStats(int channel, ivec2 pixel, ivec2 size, uint meshID,
    out vec3 mean, out vec3 stddev)
{
    vec3  sum = vec3(0.0), sumSq = vec3(0.0);
    float count = 0.0;
    for (int dy = -2; dy <= 2; ++dy)
    for (int dx = -2; dx <= 2; ++dx)
    {
        ivec2 t = pixel + ivec2(dx, dy);
        if (t.x < 0 || t.y < 0 || t.x >= size.x || t.y >= size.y) continue;
        if (floatBitsToUint(imageLoad(u_CurrPositionMeshID, t).w) != meshID) continue;
        vec3 c = clamp(channel == 0 ? imageLoad(u_RadianceDiffuse, t).rgb
                                    : imageLoad(u_RadianceSpecular, t).rgb,
                       vec3(0.0), vec3(65504.0));
        sum   += c;
        sumSq += c * c;
        count += 1.0;
    }
    mean = count > 0.0 ? sum / count
        : clamp(channel == 0 ? imageLoad(u_RadianceDiffuse, pixel).rgb
                             : imageLoad(u_RadianceSpecular, pixel).rgb,
                vec3(0.0), vec3(65504.0));
    stddev = count > 0.0 ? sqrt(max(sumSq / count - mean * mean, vec3(0.0))) : vec3(0.0);
}

// Does the previous-frame sample at `tap` describe the SAME world surface as the
// current pixel? One predicate shared by the bilinear taps, the reprojection
// search, and (with reflCheck) the virtual-reflection validation.
bool ConsistentTap(ivec2 tap, ivec2 size, vec3 normal, vec3 currWorldPos,
    uint meshID, float posGradient, float nMin, float planeTol,
    bool reflCheck, float refZ, float refDist)
{
    if (tap.x < 0 || tap.y < 0 || tap.x >= size.x || tap.y >= size.y)
        return false;
    vec4 prevG = imageLoad(u_PrevHistoryGBuffer, tap);
    if (floatBitsToUint(prevG.w) != meshID)
        return false;
    if (dot(normal, OctDecode(prevG.xy)) < nMin)
        return false;
    vec3 tapWP = imageLoad(u_CurrPositionMeshID, tap).xyz;
    if (abs(dot(tapWP - currWorldPos, normal)) > planeTol * posGradient)
        return false;
    if (reflCheck)
    {
        vec2 pr = imageLoad(u_SpecReflPrev, tap).xy; // prev virtual view-Z, hit dist
        if (pr.y < 1e-4)
            return false;
        if (abs(refZ    - pr.x) > CERBERUS_VIRTUAL_Z_REL_TOL    * max(abs(refZ), 1.0))
            return false;
        if (abs(refDist - pr.y) > CERBERUS_VIRTUAL_DIST_REL_TOL * max(refDist, 1.0))
            return false;
    }
    return true;
}

// Reproject one channel's history to this pixel along `motion`: bilinear gather
// with per-tap surface mesh/normal/depth consistency, a bounded search fallback,
// then normalise. `normalMinDot` is the surface-normal threshold (loose for virtual).
// When `reflCheck`, a tap must ALSO agree on the reflection geometry: the virtual
// view-Z and reflected-ray hit distance stored for it last frame (u_SpecReflPrev)
// must match `refZ` / `refDist` -- this is what validates a virtual reprojection
// (point 4), instead of the surface G-buffer. Returns false on disocclusion.
bool GatherHistory(int channel, vec2 motion, ivec2 pixel, ivec2 size,
    vec3 normal, vec3 currWorldPos, uint meshID, float posGradient, float complexity,
    float normalMinDot, bool reflCheck, float refZ, float refDist,
    out vec3 histColor, out vec2 histMoments, out float histLength)
{
    histColor = vec3(0.0); histMoments = vec2(0.0); histLength = 0.0;

    // NRD-style curvature relaxation: where the local surface is genuinely
    // complex (folded curtain, foliage), a 1-px reprojection error legitimately
    // lands on a different micro-facet -- loosen the normal and plane tests so
    // the pixel is not falsely disoccluded. Flat surfaces (complexity ~ 0) keep
    // the strict tolerances, so this cannot introduce ghosting there.
    float nMin     = normalMinDot - 0.35 * complexity;
    float planeTol = CERBERUS_PLANE_TOLERANCE * mix(1.0, 5.0, complexity);

    vec2  prevPix    = (vec2(pixel) + 0.5) + motion * vec2(size);
    vec2  weightFrac = fract(prevPix - 0.5);
    ivec2 baseTap    = ivec2(floor(prevPix - 0.5));
    float bilinear[4] = float[4](
        (1.0 - weightFrac.x) * (1.0 - weightFrac.y),
        weightFrac.x         * (1.0 - weightFrac.y),
        (1.0 - weightFrac.x) * weightFrac.y,
        weightFrac.x         * weightFrac.y);
    ivec2 tapOffset[4] = ivec2[4](ivec2(0, 0), ivec2(1, 0), ivec2(0, 1), ivec2(1, 1));

    float weightSum = 0.0;
    for (int i = 0; i < 4; ++i)
    {
        ivec2 tap = baseTap + tapOffset[i];
        if (!ConsistentTap(tap, size, normal, currWorldPos, meshID, posGradient,
                nMin, planeTol, reflCheck, refZ, refDist))
            continue;

        float w = bilinear[i];
        vec3  c = channel == 0 ? imageLoad(u_PrevColorHistoryDiff, tap).rgb
                               : imageLoad(u_PrevColorHistorySpec, tap).rgb;
        vec4  m = channel == 0 ? imageLoad(u_PrevMomentsDiff, tap)
                               : imageLoad(u_PrevMomentsSpec, tap);
        histColor   += w * c;
        histMoments += w * m.xy;
        histLength  += w * m.z;
        weightSum   += w;
    }

    if (weightSum < 1e-4)
    {
        // ReLAX-style reprojection search: the bilinear footprint missed because
        // the motion vector is only pixel-accurate. Scan a small window for the
        // NEAREST consistent previous pixel before declaring a disocclusion --
        // otherwise a fast pan resets a band of already-converged, fully-visible
        // geometry to 1-spp noise. A genuine disocclusion still resets: the
        // revealed surface has no coplanar same-mesh previous neighbour here.
        ivec2 centerTap  = ivec2(round(prevPix - 0.5));
        float bestDist2  = 1e9;
        ivec2 bestTap    = centerTap;
        for (int sy = -CERBERUS_REPROJ_SEARCH; sy <= CERBERUS_REPROJ_SEARCH; ++sy)
        for (int sx = -CERBERUS_REPROJ_SEARCH; sx <= CERBERUS_REPROJ_SEARCH; ++sx)
        {
            ivec2 t = centerTap + ivec2(sx, sy);
            if (!ConsistentTap(t, size, normal, currWorldPos, meshID, posGradient,
                    nMin, planeTol, reflCheck, refZ, refDist))
                continue;
            float d2 = float(sx * sx + sy * sy);
            if (d2 < bestDist2) { bestDist2 = d2; bestTap = t; }
        }
        if (bestDist2 < 1e9)
        {
            histColor   = channel == 0 ? imageLoad(u_PrevColorHistoryDiff, bestTap).rgb
                                       : imageLoad(u_PrevColorHistorySpec, bestTap).rgb;
            vec4 m = channel == 0 ? imageLoad(u_PrevMomentsDiff, bestTap)
                                  : imageLoad(u_PrevMomentsSpec, bestTap);
            histMoments = m.xy; histLength = m.z;
            weightSum = 1.0;
        }
    }

    if (weightSum < 1e-4)
        return false;

    float inv = 1.0 / weightSum;
    histColor *= inv; histMoments *= inv; histLength *= inv;
    return true;
}

struct ChannelOut
{
    vec3  illum;
    vec2  moments;
    float historyLength;
    float variance;
    float appliedAlpha;  // diagnostic + read by SVGFAtrousSplit
};

// One channel's temporal blend. Reference form (A-SVGF adaptive alpha + TAA/ReLAX
// neighbourhood clamp): firefly-clamp the incoming sample; clamp the reprojected
// history to the current 3x3 colour AABB (kills ghosting without a history
// reset); shorten history only by the temporal gradient (a real lighting change,
// camera-motion-invariant); blend at alpha = max(alphaMin, 1/historyLength,
// gradient).
ChannelOut ProcessChannel(
    bool reset,
    vec3 currIllum, float currLuma,
    vec3 histColor, vec2 histMoments, float histLength,
    vec3 neighMean, vec3 neighStdDev, float ringMax,
    float alphaMin, float gradient)
{
    ChannelOut o;

    o.appliedAlpha = 1.0;

    if (reset)
    {
        o.illum    = currIllum;
        o.moments  = vec2(currLuma, currLuma * currLuma);
        o.historyLength   = 1.0;
        o.variance = 0.0;
        return o;
    }

    // Firefly clamp: PURELY spatial (NRD anti-firefly). Cap the incoming sample to
    // 4x the brightest same-surface neighbour this frame. No temporal term: the
    // old `mean + 4*stddev` tightened as the history converged (stddev -> 0),
    // clipped ordinary 1-spp noise, and the clipped mean drifted down through the
    // EMA feedback -- the whole image slowly darkened. A spatial-only ceiling
    // never tightens, so the EMA converges to the true (un-clipped) mean.
    if (ringMax > 0.0)
    {
        float maxLuma = 4.0 * ringMax + 0.3;
        if (currLuma > maxLuma)
        {
            currIllum *= maxLuma / currLuma;
            currLuma = maxLuma;
        }
    }

    // Only the temporal gradient shortens history. Camera motion with a valid
    // reprojection does not. Full reset (gradient = 1) MUST pair with a full
    // historyLength drop -- alpha jumps to 1 this frame regardless, and unless
    // historyLength also drops, the following frames get alpha = 1/historyLength
    // (still tiny) and the pixel freezes on that one noisy raw sample instead of
    // re-converging through the normal 1, 1/2, 1/3... ramp. (A partial cut here
    // was tried and made motion noise worse for exactly this reason.)
    histLength = mix(histLength, 0.0, gradient);
    o.historyLength = min(histLength + 1.0, CERBERUS_MAX_HISTORY);

    // Neighbourhood variance clip -- ONLY where the A-SVGF gradient says the
    // lighting at this world point actually changed (swept shadow, toggled
    // light), i.e. where the reprojected history is genuinely stale. This is how
    // Lumen's ScreenProbe temporal denoiser gates its clamp
    // (LumenScreenProbeGatherTemporal.usf: `if (FastUpdateModeAmount > 0)`), and
    // FastUpdateModeAmount is a temporal lighting-change signal, NOT history
    // length. Gating on "young" instead (the old historyLength < 32) makes the
    // clamp fire on the WHOLE screen during any sustained camera pan -- no pixel
    // reaches 32 frames while moving -- which pulls every pixel toward its local
    // 5x5 mean: light and shadow bleed across junction edges, and lit regions
    // darken toward the boundary-straddling mean. Under pure camera motion the
    // gradient is ~0, so now the geometry-validated reprojection just passes
    // through untouched.
    vec3 histClamped = histColor;
    if (gradient > 0.0)
    {
        histClamped = clamp(histColor,
            neighMean - CERBERUS_CLIP_GAMMA_FRESH * neighStdDev,
            neighMean + CERBERUS_CLIP_GAMMA_FRESH * neighStdDev);
    }

    float alpha = max(max(alphaMin, 1.0 / o.historyLength), gradient);
    o.appliedAlpha = alpha;
    o.illum    = mix(histClamped, currIllum, alpha);
    o.moments  = mix(histMoments, vec2(currLuma, currLuma * currLuma), alpha);
    o.variance = max(0.0, o.moments.y - o.moments.x * o.moments.x);
    return o;
}

void main()
{
    ivec2 pixel = ivec2(gl_GlobalInvocationID.xy);
    ivec2 size = imageSize(u_CurrNormalDepth);
    if (pixel.x >= size.x || pixel.y >= size.y)
        return;

    vec4  currND       = imageLoad(u_CurrNormalDepth, pixel);
    vec3  normal       = currND.xyz;
    float linearDepth  = currND.w;
    vec4  currPos      = imageLoad(u_CurrPositionMeshID, pixel);
    vec3  currWorldPos = currPos.xyz;
    uint  meshID       = floatBitsToUint(currPos.w);

    vec3  diffIllum = imageLoad(u_RadianceDiffuse, pixel).rgb;
    vec3  specIllum = imageLoad(u_RadianceSpecular, pixel).rgb;
    // Sanitise: a NaN/Inf from a degenerate demodulation would otherwise enter
    // the ping-pong history and never clear. Also cap the magnitude so E[l^2]
    // stays finite.
    if (any(isnan(diffIllum)) || any(isinf(diffIllum))) diffIllum = vec3(0.0);
    if (any(isnan(specIllum)) || any(isinf(specIllum))) specIllum = vec3(0.0);
    diffIllum = min(diffIllum, vec3(65504.0));
    specIllum = min(specIllum, vec3(65504.0));
    float diffLuma  = Luminance(diffIllum);
    float specLuma  = Luminance(specIllum);

    // Sky: nothing to reproject; pass both channels through with zero variance.
    if (meshID == CERBERUS_SKY_MESH_ID)
    {
        imageStore(u_OutTemporalColorDiff, pixel, vec4(diffIllum, 0.0));
        imageStore(u_OutMomentsDiff, pixel, vec4(diffLuma, diffLuma * diffLuma, 0.0, 0.0));
        imageStore(u_OutTemporalColorSpec, pixel, vec4(specIllum, 0.0));
        imageStore(u_OutMomentsSpec, pixel, vec4(specLuma, specLuma * specLuma, 0.0, 0.0));
        imageStore(u_OutHistoryGBuffer, pixel, PackHistoryGBuffer(normal, linearDepth, meshID));
        imageStore(u_OutSpecReflHistory, pixel, vec4(0.0));
        imageStore(u_OutDirectLuma, pixel, vec4(0.0));
        // Keep the ColorHistory copy fresh for sky too, so HistoryFix / the sky
        // -> geometry reprojection never read a stale value here.
        imageStore(u_OutColorHistoryDiff, pixel, vec4(diffIllum, 0.0));
        imageStore(u_OutColorHistorySpec, pixel, vec4(specIllum, 0.0));
        return;
    }

    // World-space size of ~one pixel on this surface, from the immediate
    // neighbours' world positions. Scales the reprojection plane tolerance.
    // The 3x3 normal spread is a cheap curvature / fold-density proxy: it relaxes
    // the reprojection consistency where the surface is genuinely complex
    // (Sponza curtains, foliage) so it is not falsely disoccluded, while flat
    // surfaces stay strict.
    float posGradient = 1e-4;
    vec3  nSum   = normal;
    float nCount = 1.0;
    for (int dy = -1; dy <= 1; ++dy)
    for (int dx = -1; dx <= 1; ++dx)
    {
        if (dx == 0 && dy == 0) continue;
        ivec2 t = pixel + ivec2(dx, dy);
        if (t.x < 0 || t.y < 0 || t.x >= size.x || t.y >= size.y) continue;
        vec4 tp = imageLoad(u_CurrPositionMeshID, t);
        if (floatBitsToUint(tp.w) != meshID) continue;
        posGradient = max(posGradient, length(currWorldPos - tp.xyz) / max(float(abs(dx) + abs(dy)), 1.0));
        nSum   += imageLoad(u_CurrNormalDepth, t).xyz;
        nCount += 1.0;
    }
    float complexity = clamp((1.0 - length(nSum / nCount)) * 12.0, 0.0, 1.0);

    vec2  surfaceMotion = imageLoad(u_MotionVector, pixel).xy;
    vec2  specMotion    = imageLoad(u_MotionVectorSpecular, pixel).xy;
    float roughness     = imageLoad(u_Material, pixel).x;
    vec2  specRef       = imageLoad(u_SpecReflCurr, pixel).xy; // virtual view-Z, hit dist

    bool frameReset = (u_Push.FrameIndex == 0u);

    // A-SVGF temporal gradient: relative change of this pixel's direct lighting
    // vs. last frame at the reprojected world point. Camera-motion-invariant
    // because it compares the SAME world surface point across time. 0 => keep
    // history while panning; > 0 => a real lighting/shadow change.
    float gradient = 0.0;
    if (!frameReset)
    {
        float dl = imageLoad(u_DirectLumaCurr, pixel).x;
        vec2  srcF = (vec2(pixel) + 0.5) + surfaceMotion * vec2(size) - 0.5;
        ivec2 srcCenter = ivec2(round(srcF));
        // Robust to reprojection error: take the SMALLEST relative difference
        // over a previous-frame search window -- if any nearby previous pixel
        // matches, this world point's direct lighting did not actually change
        // (so already-visible geometry stays at gradient ~ 0 while panning).
        // Same CERBERUS_REPROJ_SEARCH window as GatherHistory's fallback search:
        // a plain 2x2 bilinear footprint is only sub-pixel accurate, and under a
        // fast pan reprojection error grows past it -- a false-positive gradient
        // here shortens historyLength on UNCHANGED, already-visible geometry,
        // which (a) dumps weight onto the noisy raw 1-spp sample (grain) and
        // (b) keeps the young-pixel neighbourhood clip active, which pulls
        // brightness toward the local spatial mean (the "opacamiento" while
        // moving, that clears once the camera stops and historyLength recovers).
        float best = 1.0;
        for (int dy = -CERBERUS_REPROJ_SEARCH; dy <= CERBERUS_REPROJ_SEARCH; ++dy)
        for (int dx = -CERBERUS_REPROJ_SEARCH; dx <= CERBERUS_REPROJ_SEARCH; ++dx)
        {
            ivec2 s = srcCenter + ivec2(dx, dy);
            if (s.x < 0 || s.y < 0 || s.x >= size.x || s.y >= size.y) continue;
            float p = imageLoad(u_PrevDirectLuma, s).x;
            best = min(best, abs(dl - p) / max(max(dl, p), CERBERUS_GRAD_EPS));
        }
        gradient = clamp((best - 0.1) * 1.25, 0.0, 1.0); // small deadzone
    }

    // Diffuse: surface reprojection.
    vec3  histColorD; vec2 histMomD; float histLenD;
    bool  validD = GatherHistory(0, surfaceMotion, pixel, size, normal, currWorldPos,
        meshID, posGradient, complexity, CERBERUS_NORMAL_MIN_DOT, false, 0.0, 0.0,
        histColorD, histMomD, histLenD);
    bool  resetD = frameReset || !validD;

    // Specular: virtual reprojection validated against the reflection geometry
    // (point 4). If the virtual vector has no valid reflection this frame, or
    // nothing validates, fall back to the surface reprojection -- never a bare
    // reset (that would darken).
    vec3  histColorS; vec2 histMomS; float histLenS;
    bool  useReflCheck = u_Push.SpecVirtualReproj != 0u &&
        specRef.y > 1e-4 && roughness < CERBERUS_SPEC_VIRTUAL_ROUGHNESS_MAX;
    bool  validS = false;
    if (useReflCheck)
        validS = GatherHistory(1, specMotion, pixel, size, normal, currWorldPos,
            meshID, posGradient, complexity, CERBERUS_NORMAL_MIN_DOT_VIRTUAL, true, specRef.x, specRef.y,
            histColorS, histMomS, histLenS);
    if (!validS)
        validS = GatherHistory(1, surfaceMotion, pixel, size, normal, currWorldPos,
            meshID, posGradient, complexity, CERBERUS_NORMAL_MIN_DOT, false, 0.0, 0.0,
            histColorS, histMomS, histLenS);
    bool  resetS = frameReset || !validS;

    // Brightest same-surface neighbour per channel (firefly clamp).
    float rmD, rxD, rvD, rcD;
    float rmS, rxS, rvS, rcS;
    RingLuma(0, pixel, size, meshID, rmD, rxD, rvD, rcD);
    RingLuma(1, pixel, size, meshID, rmS, rxS, rvS, rcS);

    // Current-frame 5x5 colour stats per channel (neighbourhood variance clip).
    vec3 meanD, stdD, meanS, stdS;
    NeighborhoodStats(0, pixel, size, meshID, meanD, stdD);
    NeighborhoodStats(1, pixel, size, meshID, meanS, stdS);

    ChannelOut od = ProcessChannel(resetD, diffIllum, diffLuma,
        histColorD, histMomD, histLenD, meanD, stdD, rxD,
        u_Push.AlphaMin, gradient);

    ChannelOut os = ProcessChannel(resetS, specIllum, specLuma,
        histColorS, histMomS, histLenS, meanS, stdS, rxS,
        u_Push.AlphaMin, gradient);

    // TemporalColor.a is 16F; clamp the variance so a very bright specular pixel
    // (E[l^2] ~ 1e5) does not overflow it. Moments (.y = E[l^2]) go to the 32F
    // buffers unclamped.
    vec4 outDiff = vec4(od.illum, min(od.variance, 60000.0));
    vec4 outSpec = vec4(os.illum, min(os.variance, 60000.0));
    imageStore(u_OutTemporalColorDiff, pixel, outDiff);
    // .w = applied blend alpha. SVGFAtrousSplit reads it to scale the per-sample
    // moment variance into the variance of the accumulated mean for its luminance
    // edge-stop, and to lean on the spatial filter while history is distrusted.
    imageStore(u_OutMomentsDiff, pixel, vec4(od.moments, od.historyLength, od.appliedAlpha));
    imageStore(u_OutTemporalColorSpec, pixel, outSpec);
    imageStore(u_OutMomentsSpec, pixel, vec4(os.moments, os.historyLength, os.appliedAlpha));
    imageStore(u_OutHistoryGBuffer, pixel, PackHistoryGBuffer(normal, linearDepth, meshID));
    // Pass this frame's reflection G-buffer into the ping-pong so next frame's
    // virtual reprojection can validate against it.
    imageStore(u_OutSpecReflHistory, pixel, vec4(specRef, 0.0, 0.0));
    imageStore(u_OutDirectLuma, pixel, vec4(imageLoad(u_DirectLumaCurr, pixel).x, 0.0, 0.0, 0.0));
    // Reprojection source for next frame (until the split a-trous takes over).
    imageStore(u_OutColorHistoryDiff, pixel, outDiff);
    imageStore(u_OutColorHistorySpec, pixel, outSpec);
}
