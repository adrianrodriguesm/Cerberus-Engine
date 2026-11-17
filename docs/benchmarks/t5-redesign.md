# T5 raster submission redesign

The scene classifies each instance once and scatters it into four contiguous
ranges: opaque back-face culled, opaque two-sided, masked back-face culled,
masked two-sided. Registration order is preserved inside each range. This replaces
the comparison sort and repeated material lookups with linear classification,
prefix offsets and stable scatter. Scratch vectors retain their capacity.

Draw-stream encoding iterates ranges directly. Every range starts an independently
decodable draw area, including empty ranges, and instance indices follow the same
order used by the instance buffer and indirect arguments.

Forward pipeline variants use an explicit configuration table. Per-pass binding
tables replace the numeric bucket tests during command recording. Opaque depth
and shadow pipelines still avoid material sampling.

Forward masked reuses the albedo sample for both alpha testing and shading.
Shared packed roughness/metalness maps use one GB sample. Independently assigned
maps and absent maps retain their existing behavior. Shadow cascade selection
counts crossed ordered splits instead of looping with an early break; equality
still selects the next cascade. Texture validity guards and alpha discard remain.

## Validation and limits

- Editor Debug and Release builds passed.
- Forward opaque, Forward masked and masked depth compile with glslc for Vulkan
  1.2 and pass spirv-val.
- Bistro loads and both Direct and Indirect execute in the editor.
- Screenshot automation did not keep a consistent camera/focus between captures;
  image equivalence remains unverified. These captures are not benchmark evidence.
- Runtime logs contain no VUID/Validation Error matches, but contain Assimp FBX
  warnings and logger formatting errors (`argument not found`). This is not a
  claim of a clean synchronization-validation run.
- No hardware register/occupancy profile or controlled before/after timing was
  collected. Source-level branch removal does not establish a VGPR reduction.
- Path tracing, resize and the other regression scenes were not revalidated in
  this change. Material layouts and the ray tracing shaders were not changed.

This change targets the raster submission and material-shading part of T5;
benchmark tooling and import heuristics are outside this refactor.

## Follow-up: PBR branch refactor

`ForwardPBR.glsl` now separates roughness/metalness sampling, shading normals,
directional lighting, IBL and emission. Roughness is sampled once before selecting
packed/shared versus separate metalness. Missing textures retain neutral factors;
the shared packed path still samples once and separate paths keep their existing
accesses and channels. Scalar factors remain clamped before and after sampling.

Primary-light shadow visibility is evaluated before the lighting loop. The first
iteration consumes that factor and later iterations use 1, avoiding a light-index
branch without duplicating the BRDF code. Shadow UV/depth bounds use one vector
test. Texture validity guards, normal-map enablement, two-sided normals, the BRDF
early return and the compile-time masked alpha test remain.

SDK 1.3.204.1 `glslc --target-env=vulkan1.2 -O` comparison against the source saved
immediately before this follow-up:

| Variant | Conditional branches before / after | SPIR-V bytes before / after |
|---|---|---|
| Opaque | 23 / 17 | 13368 / 12628 |
| Masked | 25 / 19 | 13824 / 13084 |

These are static SPIR-V counts, not native GPU branch counts or timings. Both
variants pass `spirv-val` with and without optimization; opaque has no discard,
masked retains one. A GLM harness adapted the old/new GLSL material expressions
to C++ (float literals and swizzle syntax only) and checked 18,000 combinations
of packed/separate/missing maps, scalar values and texture contents. Outputs and
texture-access sequences matched exactly. Boundary cases for shadow coordinates
and primary-only visibility for 0–4 lights also passed.

Artifacts are `intermediate/pbr-before-*`, `pbr-after-*`, `pbr-debug-*` and
`pbr-refactor-test*`. This follow-up does not establish visual equivalence, native
register/occupancy changes or a frame-time improvement; controlled GPU captures
and timing comparisons remain pending.
