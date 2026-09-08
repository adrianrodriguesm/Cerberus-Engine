#type compute
#version 450 core

// Edge-aware a-trous wavelet filter for one demodulated signal of the
// diffuse/specular split (Schied et al. 2017), variance-guided. 5x5 B3-spline
// kernel; the caller runs it N times per channel with a doubling step size and
// ping-pongs the colour buffers. Iteration 0's output is the reprojection source
// next frame's temporal pass reads (Schied's post-first-iteration feedback).
//
// The luminance edge-stop is guided by the variance of the accumulated MEAN
// (per-sample moment variance * alpha/(2-alpha)), read from the temporal moments
// on every iteration -- see MeanVarianceFromMoments. That keeps converged shadow
// and contact edges sharp at a high SigmaLuma instead of melting them, and it is
// consistent across all iterations because the moments are not ping-ponged.
//
// Channel selects the tuning:
//   0 diffuse  -> the paper's behaviour (no roughness scaling).
//   1 specular -> near-mirror metal carries a noisy reflection the temporal pass
//                 cannot fully converge and, having been demodulated, no albedo
//                 detail to protect. So there: filter luminance HARDER (bigger
//                 sigma) and RELAX the normal stop so the wide steps can smooth
//                 along a curved barrel. mesh-id + depth stops still bound it.
//
// Buffers are RGBA16F here (half the VRAM); moments stay RGBA32F because E[l^2]
// of a bright specular pixel overflows 16F. filteredVariance is clamped so it
// does not overflow the 16F .a channel it is written to.

layout(local_size_x = 16, local_size_y = 16, local_size_z = 1) in;

layout(binding = 0, rgba16f) readonly  uniform image2D u_InColor;         // .rgb illumination, .a variance
layout(binding = 1, rgba32f) readonly  uniform image2D u_NormalDepth;     // .xyz world normal, .w linear view depth
layout(binding = 2, rgba32f) readonly  uniform image2D u_PositionMeshID;  // .w uintBitsToFloat(meshID)
layout(binding = 3, rgba32f) readonly  uniform image2D u_Moments;         // .x E[l] .y E[l^2] .z history length .w applied alpha
layout(binding = 4, rgba16f) writeonly uniform image2D u_OutColor;
layout(binding = 5, rg16f)   readonly  uniform image2D u_Material;        // .x roughness, .y metalness
layout(binding = 6, rg16f)   readonly  uniform image2D u_HitDistance;    // .x camera->hit, .y hit->specular bounce hit

layout(push_constant) uniform Push
{
    int   StepSize;
    float SigmaLuma;
    float SigmaDepth;
    uint  FirstIteration;
    uint  FrameIndex;   // unused here (no tap-grid jitter -- see main); kept for ABI
    uint  Channel;      // 0 diffuse, 1 specular
} u_Push;

const uint  CERBERUS_SKY_MESH_ID  = 0xFFFFFFFFu;
const float CERBERUS_SIGMA_NORMAL = 128.0;
const float CERBERUS_SPATIAL_HISTORY_THRESHOLD = 4.0;
const float CERBERUS_MAX_HISTORY = 512.0;     // must match SVGFTemporalSplit
const float CERBERUS_VARIANCE_MAX = 60000.0; // keep the 16F .a channel finite
// Specular guide-variance floor (scaled by specularTightness). Keeps the spatial
// filter barely alive on smooth metal after the temporal variance bottoms out,
// without forcing enough blur to lose the reflected detail -- small on purpose.
const float CERBERUS_SPEC_VARIANCE_FLOOR = 0.006;
// Step 8.2: how tight the specular a-trous may pull in for a near-contact
// reflection (virtualDist << primaryDist). 1.0 = the fixed reach; the filter
// never widens past that. Only mirror-like pixels (specularTightness) are pulled
// in -- a rough reflection keeps the full reach because its lobe footprint is
// genuinely wide.
const float CERBERUS_SPEC_MIN_STEP_SCALE = 0.25;
// Cap on the luma edge-stop denominator, as a fraction of the pixel's luminance.
// 0.4 => a luminance jump larger than ~0.4x the pixel value stops the filter,
// regardless of a noisy guideVariance -- prevents dark-into-light bleed.
const float CERBERUS_LUMA_STOP_MAX_FRAC = 0.4;
// History-distrust ramp (specular). Driven by HISTORY LENGTH -- full boost only
// for a genuinely fresh pixel (disocclusion); zero for anything with more than
// CERBERUS_DISTRUST_HISTORY frames. Deliberately NOT driven by the temporal
// blend alpha, which camera motion inflates -- boosting the spatial filter there
// just blurs mechanical surfaces the reprojection already kept sharp.
const float CERBERUS_DISTRUST_HISTORY   = 6.0;
// Luma-sigma multiplier at full distrust (fresh pixel only).
const float CERBERUS_DISTRUST_LUMA_BOOST = 3.0;

const float CERBERUS_KERNEL[5] = float[5](1.0 / 16.0, 1.0 / 4.0, 3.0 / 8.0, 1.0 / 4.0, 1.0 / 16.0);

float Luminance(vec3 c) { return dot(c, vec3(0.2126, 0.7152, 0.0722)); }

// 3x3 Gaussian of the variance of the accumulated MEAN (not of a single sample),
// read from the moments the split temporal pass wrote this frame:
//   perSampleVar = E[l^2] - E[l]^2         (per-frame Monte-Carlo noise)
//   meanVar      = perSampleVar * a/(2-a)  (residual noise left after the EWMA,
//                                           a = the applied blend alpha)
// This is what the luminance edge-stop should compare a spatial luma delta
// against: a pixel with deep history has meanVar ~ 0, so a converged shadow /
// contact edge stops the filter sharply even at a high SigmaLuma, while a freshly
// disoccluded pixel (a ~ 1) keeps the full variance and the full smoothing.
// u_Moments is not ping-ponged, so this is valid on every a-trous iteration --
// unlike u_InColor.a, which the filter itself shrinks pass over pass and which,
// once the first pass has blurred a penumbra, no longer knows an edge was there.
float MeanVarianceFromMoments(ivec2 pixel, ivec2 size)
{
    const float weights[3] = float[3](1.0 / 4.0, 1.0 / 8.0, 1.0 / 16.0);
    float sum = 0.0;
    float weightSum = 0.0;
    for (int dy = -1; dy <= 1; ++dy)
    {
        for (int dx = -1; dx <= 1; ++dx)
        {
            ivec2 q = clamp(pixel + ivec2(dx, dy), ivec2(0), size - ivec2(1));
            vec4  m = imageLoad(u_Moments, q);
            float perSampleVar = max(m.y - m.x * m.x, 0.0);
            float alphaEff = clamp(m.w, 1.0 / CERBERUS_MAX_HISTORY, 1.0);
            float w = weights[abs(dx) + abs(dy)];
            sum += w * perSampleVar * alphaEff / (2.0 - alphaEff);
            weightSum += w;
        }
    }
    return sum / weightSum;
}

float SpatialLumaVariance(ivec2 pixel, ivec2 size, vec3 centerNormal, float centerDepth,
    uint centerMesh, float depthGradient)
{
    float weightSum = 0.0;
    float lumaSum = 0.0;
    float lumaSqSum = 0.0;

    for (int dy = -3; dy <= 3; ++dy)
    {
        for (int dx = -3; dx <= 3; ++dx)
        {
            ivec2 tap = pixel + ivec2(dx, dy);
            if (tap.x < 0 || tap.y < 0 || tap.x >= size.x || tap.y >= size.y)
                continue;

            vec4 tapND = imageLoad(u_NormalDepth, tap);
            if (floatBitsToUint(imageLoad(u_PositionMeshID, tap).w) != centerMesh)
                continue;

            float normalWeight = pow(max(dot(centerNormal, tapND.xyz), 0.0), CERBERUS_SIGMA_NORMAL);
            float depthWeight = exp(-abs(centerDepth - tapND.w) /
                (depthGradient * length(vec2(dx, dy)) + 1e-4));
            float spatialWeight = exp(-0.5 * float(dx * dx + dy * dy) / 4.0);
            float w = normalWeight * depthWeight * spatialWeight;

            float luma = Luminance(imageLoad(u_InColor, tap).rgb);
            lumaSum   += w * luma;
            lumaSqSum += w * luma * luma;
            weightSum += w;
        }
    }

    if (weightSum < 1e-6)
        return 0.0;

    float mean = lumaSum / weightSum;
    return clamp(lumaSqSum / weightSum - mean * mean, 0.0, CERBERUS_VARIANCE_MAX);
}

void main()
{
    ivec2 pixel = ivec2(gl_GlobalInvocationID.xy);
    ivec2 size = imageSize(u_InColor);
    if (pixel.x >= size.x || pixel.y >= size.y)
        return;

    vec4  centerColor  = imageLoad(u_InColor, pixel);
    vec3  centerIllum  = centerColor.rgb;
    vec4  centerND     = imageLoad(u_NormalDepth, pixel);
    vec3  centerNormal = centerND.xyz;
    float centerDepth  = centerND.w;
    uint  centerMesh   = floatBitsToUint(imageLoad(u_PositionMeshID, pixel).w);

    if (centerMesh == CERBERUS_SKY_MESH_ID)
    {
        imageStore(u_OutColor, pixel, centerColor);
        return;
    }

    float depthGradient = 1e-3;
    if (pixel.x + 1 < size.x) depthGradient = max(depthGradient, abs(centerDepth - imageLoad(u_NormalDepth, pixel + ivec2(1, 0)).w));
    if (pixel.x - 1 >= 0)     depthGradient = max(depthGradient, abs(centerDepth - imageLoad(u_NormalDepth, pixel - ivec2(1, 0)).w));
    if (pixel.y + 1 < size.y) depthGradient = max(depthGradient, abs(centerDepth - imageLoad(u_NormalDepth, pixel + ivec2(0, 1)).w));
    if (pixel.y - 1 >= 0)     depthGradient = max(depthGradient, abs(centerDepth - imageLoad(u_NormalDepth, pixel - ivec2(0, 1)).w));

    vec4  centerMoments = imageLoad(u_Moments, pixel);
    bool  isYoung = centerMoments.z < CERBERUS_SPATIAL_HISTORY_THRESHOLD;
    bool  useSpatialVariance = u_Push.FirstIteration != 0u && isYoung;

    float guideVariance;
    if (useSpatialVariance)
    {
        // Disocclusion, first pass: the moments are (l, l^2) from a single sample
        // so MeanVarianceFromMoments is identically 0 -- bootstrap from the 7x7
        // spatial luma variance instead (SVGF 4.2).
        guideVariance = SpatialLumaVariance(pixel, size, centerNormal, centerDepth,
            centerMesh, depthGradient);
    }
    else
    {
        guideVariance = MeanVarianceFromMoments(pixel, size);
        // Iterations 1..N of a STILL-young pixel: its moments are still ~0, so the
        // luma edge-stop would clamp shut and freeze 1-spp noise into a
        // disoccluded shadowed corner for the rest of the chain. SVGF filters the
        // variance with the same a-trous kernel each pass and carries it in the
        // colour buffer's alpha -- do that here. Converged pixels have
        // isYoung == false, so their contact/shadow edges stay governed by the
        // near-zero mean variance and do NOT soften (Step 7's behaviour kept).
        if (isYoung)
            guideVariance = max(guideVariance, centerColor.a);
    }

    // specularTightness only applies on the specular channel.
    vec2  material = imageLoad(u_Material, pixel).xy;
    float specularTightness = (u_Push.Channel == 1u)
        ? clamp((1.0 - material.x) * (0.3 + 0.7 * material.y), 0.0, 1.0)
        : 0.0;

    // History-distrust boost (specular): full only for a genuinely fresh pixel
    // (disocclusion), from centerMoments.z (history length). NOT from the temporal
    // blend alpha -- camera motion inflates that, and boosting the spatial filter
    // there just blurs mechanical surfaces the reprojection already kept sharp.
    float historyDistrust = (u_Push.Channel == 1u)
        ? 1.0 - clamp(centerMoments.z / CERBERUS_DISTRUST_HISTORY, 0.0, 1.0)
        : 0.0;

    // Metal has no albedo detail to protect and a noisier reflection, so filter
    // its luminance a little harder than diffuse -- but only a little: a big
    // multiplier here melts the reflected room / scratches off a mirror. The
    // temporal pass and the Step-7 mean-variance guide carry most of the noise.
    float sigmaLuma   = mix(u_Push.SigmaLuma, u_Push.SigmaLuma * 1.5, specularTightness);
    sigmaLuma = mix(sigmaLuma, sigmaLuma * CERBERUS_DISTRUST_LUMA_BOOST, historyDistrust);
    // Soften the normal power on glossy metal so the wide steps can smooth along
    // a gentle curve, but keep it high enough that a helmet's curvature still
    // stops the filter -- 16 let reflections slide across the whole dome.
    float sigmaNormal = mix(CERBERUS_SIGMA_NORMAL, 64.0, specularTightness);
    sigmaNormal = mix(sigmaNormal, 24.0, historyDistrust);

    // Keep the spatial filter alive on smooth metal after the temporal variance
    // has bottomed out (see CERBERUS_SPEC_VARIANCE_FLOOR).
    guideVariance = max(guideVariance, specularTightness * CERBERUS_SPEC_VARIANCE_FLOOR);

    // Step 8.2: pull the specular filter reach in for a near-contact reflection.
    // The GGX lobe's screen footprint at the primary surface scales with
    // virtualDist / (primaryDist + virtualDist): tiny when the reflected point is
    // close to the surface (crisp reflection to preserve), full when it is far
    // (sky / distant wall). Mirror-like pixels only; never widens past StepSize.
    float effectiveStep = float(u_Push.StepSize);
    if (u_Push.Channel == 1u && specularTightness > 0.0)
    {
        vec2  hitDist = imageLoad(u_HitDistance, pixel).xy;
        float pathFraction = hitDist.y / max(hitDist.x + hitDist.y, 1e-3);
        float tightScale = mix(CERBERUS_SPEC_MIN_STEP_SCALE, 1.0, clamp(pathFraction, 0.0, 1.0));
        effectiveStep *= mix(1.0, tightScale, specularTightness);
    }
    // Widen the reach while the history is distrusted so the harder filter also
    // covers larger noise blobs left by the near-reset EMA.
    effectiveStep *= mix(1.0, 2.0, historyDistrust);

    float centerLuma = Luminance(centerIllum);
    // Variance-guided luma edge stop, but CAPPED at a fraction of the pixel's own
    // luminance: an over-estimated guideVariance (the 1-spp moment variance is
    // noisy) must not let the filter bleed a deep shadow / black opening into an
    // adjacent lit surface of the same mesh. A >~50% luminance jump always stops.
    float lumaDenom = min(sigmaLuma * sqrt(max(guideVariance, 0.0)),
                          CERBERUS_LUMA_STOP_MAX_FRAC * centerLuma) + 1e-6;

    // No per-frame tap-grid jitter here. Unlike the single-signal chain, the
    // split a-trous output (iterations 1..N) goes straight to Finalize with no
    // temporal averaging behind it, so a jittered grid would show up directly as
    // per-frame flicker on a still camera -- most visibly on metal, where the
    // filter is working hardest. A static input must give a static output.
    vec2 tapJitter = vec2(0.0);

    float centerKernel = CERBERUS_KERNEL[2] * CERBERUS_KERNEL[2];
    vec3  sumColor    = centerIllum * centerKernel;
    float sumVariance = centerColor.a * centerKernel * centerKernel;
    float sumWeight   = centerKernel;

    for (int dy = -2; dy <= 2; ++dy)
    {
        for (int dx = -2; dx <= 2; ++dx)
        {
            if (dx == 0 && dy == 0)
                continue;

            ivec2 tap = pixel + ivec2(round((vec2(dx, dy) + tapJitter) * effectiveStep));
            if (tap.x < 0 || tap.y < 0 || tap.x >= size.x || tap.y >= size.y)
                continue;

            vec4 tapND  = imageLoad(u_NormalDepth, tap);
            uint tapMesh = floatBitsToUint(imageLoad(u_PositionMeshID, tap).w);
            if (tapMesh != centerMesh)
                continue;

            vec4  tapColor = imageLoad(u_InColor, tap);

            float kernelWeight = CERBERUS_KERNEL[dx + 2] * CERBERUS_KERNEL[dy + 2];
            float normalWeight = pow(max(dot(centerNormal, tapND.xyz), 0.0), sigmaNormal);

            float tapDistance = length(vec2(dx, dy) * effectiveStep);
            float depthWeight = exp(-abs(centerDepth - tapND.w) /
                (u_Push.SigmaDepth * depthGradient * tapDistance + 1e-4));

            float lumaWeight = exp(-abs(centerLuma - Luminance(tapColor.rgb)) / lumaDenom);

            float weight = kernelWeight * normalWeight * depthWeight * lumaWeight;

            sumColor    += weight * tapColor.rgb;
            sumVariance += weight * weight * tapColor.a;
            sumWeight   += weight;
        }
    }

    vec3  filteredColor    = sumWeight > 0.0 ? sumColor / sumWeight : centerIllum;
    float filteredVariance = sumWeight > 0.0 ? sumVariance / (sumWeight * sumWeight) : centerColor.a;
    if (useSpatialVariance)
        filteredVariance = max(filteredVariance, guideVariance);
    filteredVariance = min(filteredVariance, CERBERUS_VARIANCE_MAX);

    imageStore(u_OutColor, pixel, vec4(filteredColor, filteredVariance));
}
