# Path Tracer Denoiser — Reference Gap Analysis

Status: investigation, 2026-09-09. Branch `pathtracer-svgf-denoiser`.

References inspected (source): UE 5.8 `MegaLightsDenoiser{Temporal,Spatial}.usf`,
`MegaLightsShading.usf`, `MegaLightsSampling.ush`, `LumenScreenProbeGatherTemporal.usf`,
`PathTracingSpatialTemporalDenoising.usf`, `SSDSpatialAccumulation.usf`,
`LumenReflectionDenoiser{Temporal,Spatial}.usf`. Plus SVGF (Schied 2017),
A‑SVGF (Schied 2018), NRD/ReLAX (docs + prior knowledge).

---

## 1. Context — what our integrator actually produces

This matters more than any denoiser detail, because it defines the signal the
denoiser is handed.

`Raytrace_rgen.rgen`:

| Aspect | Cerberus today |
| --- | --- |
| Local lights (point/spot/rect/sphere) | **none** — not implemented |
| Emissive‑mesh lights | **none** |
| Directional lights | NEE, delta distribution, 1 shadow ray, **zero MC variance** |
| Sky / IBL | **BSDF‑importance‑sampled only** — on a ray miss the miss shader returns the env map in the ray direction. No env importance sampling, no NEE for the env, no MIS. |
| Indirect (GI) | BSDF‑sampled bounces, 1 spp, `Max bounces = 4` |
| Sampler | per‑frame RNG decorrelation (`m_DenoiserFrameIndex`), not a spatiotemporal blue‑noise / Owen‑scrambled Sobol sequence |
| Adaptive sampling | none (uniform 1 spp everywhere) |

**Consequence:** in the test scene (1 directional + skylight) the hard shadow of
the gun is *deterministic and noise‑free*. Everything the user perceives as
"shadows/fine shadows taking a few frames to build" is **indirect illumination
and contact occlusion (AO) computed by 1‑spp cosine/BSDF sampling of the
skylight** — a mid‑frequency, partially‑occluded environment sampled with a
strategy that ignores where the light is and whether it is blocked. That is the
single largest source of the residual noise, and it is a *sampling* problem, not
a *denoiser* problem.

---

## 2. What Cerberus already implements (denoiser side)

So the gap list below is read against a real baseline, not zero.

- Demodulated diffuse / specular split (÷ diffuse albedo, ÷ preintegrated env BRDF); emissive kept raw; recombine in Finalize.
- Temporal reprojection: surface motion vector, bilinear taps, per‑tap mesh/normal/**world‑space plane** consistency, **ReLAX‑style ±2 px search fallback**, **tap‑distance‑scaled plane tolerance** (fixes curved‑surface reset under motion).
- A‑SVGF adaptive alpha `max(alphaMin, 1/N, gradient)`, `alphaMin = 0` (progressive mean when still).
- A‑SVGF temporal gradient — **approximation**: raw 1‑spp direct luma vs the *temporally accumulated* direct‑luma mean, band sized from the **spatial** variance of the current direct luma, min over the search window, scaled by `AntilagStrength`.
- Neighbourhood colour clip gated to `gradient > 0` (Lumen `FastUpdateMode` rectify model) — not on every young pixel.
- Virtual (reflected‑ray) specular reprojection validated against a reflection G‑buffer (`u_SpecReflPrev`: virtual view‑Z + reflected‑ray hit distance); roughness‑blended surface↔virtual motion vector; specular decoupled from the direct‑luma gradient.
- HistoryFix pass (ReLAX): strided wide bilateral of neighbours' *accumulated* history for short‑history pixels, illumination edge stop.
- À‑trous: 5×5 B3‑spline, N iterations, variance‑guided luma stop with `MeanVarianceFromMoments` (variance of the mean, `·α/(2−α)`), 7×7 spatial‑variance bootstrap for `historyLength < 4` on iteration 0, luma‑stop denominator capped at `0.4·centerLuma` (anti‑bleed), hit‑distance‑scaled specular reach.
- Spatial pre‑resolve pass (`SVGFSpatialPreResolve`, toggle): small fixed edge‑aware bilateral over the raw 1‑spp signal, MegaLights‑style gated to high‑relative‑variance regions. Off by user preference — see §4.
- Firefly clamp: purely spatial (`4·ringMax + 0.3`).
- `HistoryGBuffer` ping‑pong (oct‑normal, linZ, meshID); Schied post‑first‑iteration feedback (à‑trous iter 0 → next frame's reprojection source).

---

## 3. Gap A — integrator / sampling (highest impact)

This is where the references get their "clean under motion" behaviour. A denoiser
cannot reconstruct information the sampler never gathered.

| # | Reference technique | Who | Cerberus | Impact for our scene |
| --- | --- | --- | --- | --- |
| A1 | **Environment importance sampling** — build a 2D CDF / alias table over the equirect skylight, sample bright directions directly | PBRT, UE PT, every production PT | ✗ (cosine/BSDF only) | **Very high.** The skylight is the *only* fill light inside shadows. Sampling its bright regions directly is a 3–5× effective‑sample gain for shadowed GI. |
| A2 | **NEE for the environment + MIS** (balance/power heuristic between the BSDF sample and the light sample) | PBRT, UE PT | ✗ (MIS not implemented at all) | **Very high.** Removes fireflies from BSDF‑sampled bright env hits and variance from cosine‑sampled dim directions. Pairs with A1. |
| A3 | **Local / area / emissive‑mesh lights** + a light BVH / power‑weighted RIS light selection | UE MegaLights (`MegaLightsSampling`), RTXDI | ✗ (directional only) | Scene‑dependent. Enables ReSTIR DI to matter at all (see A4). |
| A4 | **ReSTIR DI** — spatiotemporal reservoir resampling of *light* samples | MegaLights, RTXDI | ✗ | **Zero for the current scene** (1 delta light → DI already exact). Only worthwhile after A3. |
| A5 | **ReSTIR GI** — reservoir temporal + spatial reuse of *indirect* path samples (resample the secondary hit / incoming radiance) | ReSTIR GI (Ouyang 2021), UE Lumen `LumenReSTIRGather` | ✗ | **High.** This is the "many effective samples per pixel per frame" that actually kills the fine‑detail‑soft‑under‑motion problem for GI. Big effort. |
| A6 | **Adaptive sampling** — variance/error map, spend the frame's ray budget where it is noisy | UE PT `PathTracingBuildAdaptiveError` / `PathTracingAdaptiveStart` | ✗ (uniform 1 spp) | Medium. Cheap once a per‑pixel variance estimate exists (the denoiser already has one). |
| A7 | **Spatiotemporal blue noise / Owen‑scrambled Sobol** sequence | UE, Heitz 2019 | partial (per‑frame RNG decorrelation, not a designed sequence) | Medium *perceptually*: same MSE, far less objectionable noise structure, denoises better. Cheap. |
| A8 | **Firefly suppression at the source** — path‑space regularisation / roughness clamping on secondary bounces, max‑radiance clamp per bounce | UE PT, Arnold | partial (final spatial firefly clamp only) | Medium. Reduces the outliers the temporal pass has to survive. |

---

## 4. Gap B — denoiser architecture

| # | Reference technique | Who | Cerberus | Notes |
| --- | --- | --- | --- | --- |
| B1 | **Spatial resolve BEFORE temporal** (`ShadeLightSamplesCS → DenoiserSpatialCS → DenoiserTemporalCS`) so the temporal EMA, the neighbourhood stats and the confidence all see a pre‑filtered, not raw‑1‑spp, signal | MegaLights | partial — `SVGFSpatialPreResolve` exists but is a small fixed kernel, gated, and toggled **off** by the user because a single‑frame spatial‑variance gate cannot separate a real edge from noise (it softened highlights / bled fine shadows). MegaLights' gate uses *temporal* moments, which need ≥1 frame of accumulation. | A correct version needs the temporal moments feeding the gate → it is really a *second* temporal‑then‑spatial arrangement, or it needs better input (Gap A). |
| B2 | **Per‑pixel shading confidence** — `WeightRatioSum` = Σ(merged samples / light importance weight), i.e. "how well did importance sampling match the actual contribution". Drives `MaxFramesAccumulated = 1/(0.5·confidence)` → poorly‑sampled pixels are allowed to accumulate longer; well‑sampled pixels converge fast. | MegaLights (`MegaLightsShading.usf`) | ✗ — accumulation speed is driven only by `historyLength` | We have no measure of *sample quality*, only *sample count*. With Gap A1/A2 in place this becomes computable (the env‑IS PDF vs the realised contribution). |
| B3 | **Dual fast/slow history** — a short‑window history for responsiveness + a long‑window history for stability, blended by a "lighting is moving" signal | NRD ReLAX, Lumen `FastUpdateMode_NumFramesAccumulated` | ✗ — single history, `historyLength`‑capped EMA | Our A‑SVGF‑gradient‑drives‑alpha is a partial substitute, but a real dual history is more robust to a noisy gradient. |
| B4 | **Neighbourhood clamp: YCoCg box, soft clamp by confidence, scale relaxed for high frame count** — `M1 ± scale·σ`, always includes the centre sample, `lerp(clamped, history, confidence)`, `scale ·= 1 + 3·LinearStep(4,2,MaxConfidenceFrames)` | MegaLights `DenoiserTemporalCS` | partial — RGB (not YCoCg) box, **hard** clamp, gated on `gradient > 0`, fixed 2.5σ, no confidence term | Needs a smooth (resolved) neighbourhood (B1) and a confidence signal (B2) to port faithfully; a direct port on raw 1‑spp stats darkens converged highlights (tried, reverted). |
| B5 | **A‑SVGF gradient via re‑traced sparse samples with the *previous frame's* RNG** — common random numbers → the MC noise cancels in the difference, so the gradient is the true change in the integrand | A‑SVGF (Schied 2018), Q2RTX | ✗ — approximated (raw‑vs‑accumulated‑mean + spatial noise band). Works, but is fundamentally a heuristic. | A real implementation is a second sparse raygen dispatch (≈1 pixel / 3×3 tile) reusing the stored seed, then its own tiny à‑trous. Meaningful effort; the correct answer if the antilag heuristic keeps mis‑firing. |
| B6 | **Continuous `DisocclusionFactor`** keeping the spatial filter engaged (wide reach, inflated spatial variance, looser sigmas) for the first ~8–16 frames after a reset, ramping off | MegaLights `DenoiserSpatialCS` | tried twice, reverted twice — "peor o igual". In this codebase it either did nothing or blurred. | Likely because the *input* is too noisy (Gap A) and the à‑trous is already near its useful limit; more filter strength just trades noise for blur. |
| B7 | **HistoryFix / disocclusion bootstrap** — wide strided bilateral of neighbours' accumulated history for freshly disoccluded pixels | ReLAX, Lumen | ✔ (`SVGFHistoryFix`) — but fails by design at geometric corners (no consistent converged neighbour exists there) | Inherent limit, shared with the references. |
| B8 | **Hit‑distance‑aware specular** — reprojection along the virtual reflection point, accumulation speed from roughness + virtual‑motion confidence, reach scaled by hit distance | NRD ReLAX, Lumen reflection denoiser | ✔ (have all three in some form) | Reasonably complete. |

---

## 5. Root cause of the residual artefact

> Fine shadows / contact occlusion stay soft for a few frames during a fast pan,
> then sharpen.

Mechanism, after the §2 fixes:

1. A pixel on a thin silhouette / geometric corner re‑disoccludes every frame under motion (no consistent previous sample within the search window — true disocclusion).
2. It resets to `historyLength = 1`: one sample of a **high‑variance 1‑spp GI signal** (Gap A — cosine‑sampled, partially occluded skylight).
3. `HistoryFix` cannot fill it (its converged neighbours are across the corner — different plane).
4. The à‑trous makes it *soft* but cannot make it *sharp and correct*: there is no information for that in one sample.
5. It converges over the `1/N` window (~10–30 frames) as the pixel stops re‑disoccluding.

**No reference denoiser fixes step 4 either.** SVGF, A‑SVGF and ReLAX all show
this. The references make it *not noticeable* by attacking steps 2 and 5:

- give the pixel a **less noisy single sample** (Gap A1/A2 env‑IS+MIS; Gap A5 ReSTIR GI reuse) — so even 1 sample is close to converged;
- give it **more effective samples per frame** (ReSTIR / higher spp).

So the fix is upstream of the denoiser.

---

## 6. Recommended roadmap

Ordered by ROI for the current engine (IBL + directional, no local lights).

| Phase | Work | Effort | Expected result |
| --- | --- | --- | --- |
| **0 (stopgap)** | `Samples/frame 1 → 2` (already exposed; ~+50 % PT cost, was 6.3 ms PT‑only). | trivial | √2 less noise everywhere; the transient becomes noticeably shorter/softer. Not a fix, a mitigation. |
| **1** | **Environment importance sampling + NEE + MIS.** 2D CDF/alias table over the equirect skylight (build once per env change, CPU or a small compute pass); sample it in NEE alongside the directional; MIS (power heuristic) between the env‑NEE sample and the BSDF sample. | medium | **Biggest single win for this scene.** 3–5× effective samples for shadowed GI; kills BSDF‑sampled env fireflies. Also unlocks Gap B2 (shading confidence) since a light PDF now exists. |
| **2** | **Spatiotemporal blue‑noise sampler** (precomputed STBN textures or Owen‑scrambled Sobol + hash). | low | Same MSE, much less objectionable grain, denoises cleaner. Do alongside Phase 1. |
| **3** | **ReSTIR GI** — reservoir per pixel over the secondary hit / incoming radiance; temporal reuse (reproject the reservoir); spatial reuse (k neighbour reservoirs, MIS/Jacobian for bias). This is the "many effective samples/frame" that actually removes the fine‑detail‑soft‑under‑motion behaviour. | high | The real fix for the residual artefact. Build after Phase 1 (shares the env‑IS + MIS infrastructure). |
| **4 (if still needed)** | Denoiser: add the shading‑confidence signal (Gap B2) and a dual fast/slow history (Gap B3); revisit the pre‑resolve (B1) now that its input is clean. | medium | Diminishing returns once Phases 1+3 land. |

Note on the earlier plan ("option 3 = ReSTIR DI"): with only a directional
(delta) light, **ReSTIR *DI* buys nothing** — DI is already exact. The relevant
variant is **ReSTIR *GI*** (Phase 3). ReSTIR DI only becomes worthwhile after
local/area/emissive lights (Gap A3) exist.

---

## 7. Immediate next steps (this session)

Per the user: proceed with Phase 0 (`Samples/frame = 2`) and Phase 3 (ReSTIR —
as **GI**, not DI). Phase 1 (env‑IS + MIS) is a cheaper prerequisite‑class win
and shares infrastructure with Phase 3; recommend doing it first or alongside.
