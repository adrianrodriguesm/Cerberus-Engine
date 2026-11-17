# T5 Step 3 — Alpha-masked materials

Scope: Step 3 only. Steps 1–2 were reviewed in `bistro-inspection.md` and in the
importer, serializers, material proxies, draw submission, and Forward pass code.
No benchmark tooling, camera bookmarks, GPU culling, blending, or Step 4 work was added.

## Materials and import

`MaterialAsset` stores `AlphaMode::{Opaque,Masked}`, `AlphaCutoff` (0.5), `TwoSided`
(false), and an optional opacity texture. Missing YAML keys retain those defaults.
The material editor exposes mode, cutoff, culling, and the linear opacity texture.
Edits notify the existing renderer callback and use the existing save path.

`GPUMaterialData` is now 96 bytes (asserted in C++). `AlphaParameters` stores cutoff,
masked flag, two-sided flag, and a reserved component. `TextureIndices2.y` is the
opacity texture index. Both Forward and RT GLSL layouts were updated together;
this does not add alpha testing to the path tracer.

Import reads glTF alpha mode/cutoff and Assimp two-sided/opacity properties, with
the inspected Bistro `.DoubleSided`/`MASKED` names as additional hints. It detects
non-opaque albedo texels in decoded ordinary images and BC1/2/3 DDS top-level data.
BC1 detection respects the DDS alpha flag because the RGB encoding also uses the
transparent selector for black. BC7 detection uses the DDS alpha flag rather than
a CPU block decoder. Opacity maps exposed under other Assimp texture types can also
be recognized by `opacity`/`alpha_mask` filenames.

Explicit glTF OPAQUE remains opaque. BLEND and Bistro BLENDSHADER materials remain
opaque-rendered: this step does not implement blending. `AlphaPropertiesImported`
persists the one-time migration of pre-existing imported materials so subsequent
loads do not overwrite editor choices. Existing non-imported material files load
with their old opaque, back-face-culled behavior.

## Draw ranges and indirect offsets

Each frame, `RenderScene::BuildDrawStream` makes a stable ordered raster-instance
list. The registration list and ray tracing instance order are left intact.
`BuildInstanceBuffer` and `BuildIndirectArgs` then traverse that same prepared list.

| Range index | Alpha bucket | Culling |
|---|---|---|
| 0 | Opaque | Back |
| 1 | Opaque | None (TwoSided) |
| 2 | Masked | Back |
| 3 | Masked | None (TwoSided) |

For counts `n0…n3`, the first instance of range `i` is the sum of counts before it.
The two alpha buckets are contiguous, and each culling subdivision is contiguous.
An indirect command remains 20 bytes, with `InstanceCount=1` and
`FirstInstance` equal to its absolute slot in the ordered instance buffer.

Every pass calls `ExecuteDrawIndirect(area, args, range.Count,
uint64_t(range.FirstInstance) * sizeof(IndirectDrawCommand))`. The offset is in
bytes, the count is in commands, and `FirstInstance` is never rebased per bucket.
For example, counts `(100, 0, 5, 20)` yield offsets `(0, 2000, 2000, 2100)` and
counts `(100, 0, 5, 20)`; the empty range issues no call. RHI asserts alignment
and buffer bounds before forwarding the offset to the native indirect command.

Direct mode uses the same ranges/counts and absolute instance slots. Each range
starts with `DrawStreamBuilder::BeginDrawArea()`, re-emitting the state needed by a
fresh decoder. Its `StreamOffset` is in uint32 words, not bytes or draw indices.
The existing submission toggle remains available. All ranges are drawn within
each existing logical pass and rendering scope; there are no extra submits.

## Pipelines

PreDepth, each shadow cascade, and Forward have opaque/masked and back/none PSOs.
Opaque depth/shadow fragment shaders remain trivial and never sample textures.
Masked depth/shadow vertices provide canonical UVs and a flat material index.
All masked fragment shaders call the shared `AlphaTest` in `RasterMaterial.glsl`:

`alpha = albedo.a * opacity.r` (missing maps contribute 1), discarding strictly
below the cutoff. Every pass uses the same V flip and sampling function. Forward
uses a shared shader body with a masked compile-time variant; opaque Forward has
no discard. Two-sided back-face shading flips the final normal.

Masked PreDepth uses the same invariant position calculation as Forward. Forward
retains `LessOrEqual`, with depth writes disabled, against the shared SceneDepth.
Masked depth passes own dedicated material/texture descriptor groups, updated with
the existing per-frame material buffer and texture registry. Extra PSOs, groups,
layouts, and shader-library entries are released on renderer destruction/switch.
Native indirect execution remains in the RHI.

## Validation

- Debug and Release builds of the engine, Editor, and Runtime passed with zero
  compiler warnings/errors. Logs: `intermediate/t5-step3-debug.log`,
  `intermediate/t5-step3-release.log`, `intermediate/t5-step3-runtime-debug.log`,
  `intermediate/t5-step3-runtime-release.log`.
- All ten affected raster stages compiled with SDK `glslc --target-env=vulkan1.2`.
  SPIR-V inspection found zero image samples and zero discard instructions in
  opaque PreDepth/Shadow; masked depth and masked Forward contain discard.
  The three ray-tracing stages also compiled against the updated material stride.
- The existing First Scene rendered Sponza, DamagedHelmet, Cerberus gun, spheres,
  and cube. Direct and Indirect captures of the 1744×1016 viewport were byte-identical
  in raw pixels (SHA-256 `73672464ea6607e99a0baec8acd5398909ee2ebd6b3ac574d8e99bb7f9be29bc`).
  This comparison verifies submission equivalence, not a pre-Step-3 screenshot diff.
- Resize/recreation was exercised. The regression run reported no Vulkan errors;
  the pre-existing `InvalidRenderpass` swapchain warning remains.
- Bistro Debug imported 1,591 instances and submitted PreDepth, the four cascades,
  and Forward without Vulkan errors, including shutdown. Synchronization validation
  was enabled using `VK_LAYER_ENABLES=VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT`;
  the installed SDK layer manifest explicitly supports this environment setting.
  Its same pre-existing swapchain warning remains. Log:
  `intermediate/t5-step3-bistro-supervised.out.log`.
- Import migrated 12 Bistro materials to Masked: all six `.DoubleSided` foliage
  materials, dirty glass, LanternEmissive, Curtains, Forge_Metal, Side_Letters,
  and Paris_LiquorBottle_01_Labels. All 13 BLENDSHADER materials stayed Opaque.
  The importer reports an unsupported `Spotlight_Emissive_Normal.dds` and falls
  back; this is an asset-loader limitation, not a Vulkan validation failure.
- Release Bistro Direct/Indirect viewport captures also matched in raw pixels
  (SHA-256 `2429b314564b818bd1052d957e8523ebe02fbb444b9b876224a684030eb8465e`).
  **This is not foliage sign-off:** that saved test camera showed mostly the
  environment, with scene geometry very small. A subsequent framed street/café
  view showed cutout ivy and foliage silhouettes. A trustworthy paired capture
  at that view was not completed: the editor lost desktop focus during automated
  capture.
  Do not interpret the saved-camera hash as close-up foliage/shadow acceptance.
- Captures and build/runtime logs remain under `intermediate/t5-*`. Startup scene,
  registry, editor layout, and tracked sample-material changes generated by the
  tests were restored; Bistro's ignored imported materials retain their migration.

### Remaining acceptance checks

Close-up Bistro foliage and masked shadows, including looking at both sides,
still need visual review and a Direct/Indirect comparison at that camera.
Pre-change screenshot diffs for the regression assets, live Forward↔PathTracer
switching, editing/saving/reloading alpha controls in the UI, and a dedicated
separate-opacity-map visual fixture were not completed. These are recorded as
unverified, not passed. Step 4 is not started by this change.

## Limits

Alpha content alone cannot always distinguish cutouts from intended blending;
explicit source mode wins, and the editor can override inferred values. BC7 alpha
detection depends on its DDS header. Embedded textures remain unsupported by the
existing importer. Opacity uses UV0 and the red channel; alternate UV sets and
texture transforms are outside this implementation. Mip alpha coverage preservation
is not added. Path-tracer any-hit alpha testing is not implemented by this Forward
step, although its shared material layout remains compatible.
