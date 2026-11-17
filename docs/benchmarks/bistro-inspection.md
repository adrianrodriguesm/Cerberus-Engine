# T5 — Bistro Exterior inspection + import report

Read-only inspection. No engine or asset-pipeline code was changed for this report. Everything below was
either read directly from the files on disk, decoded from the raw DDS/FBX bytes, or observed in a live run
of the editor against `Bistro.cscene` (stdout log + Vulkan validation output). Where a number comes from a
live run rather than a file, it says so.

## 0. Asset acquisition — already done, pre-dates this task

The Bistro Exterior FBX, its 622 source DDS textures, and an already-imported `BistroExterior.cmesh` +
132 `.cmaterial` files + `Bistro.cscene` were already present in the working tree before this task started
(most likely imported by hand in an earlier session). They live under:

- `Cerberus-Editor/Resources/Meshes/Source/Bistro_v5_2/` (FBX + `Textures/`)
- `Cerberus-Editor/Resources/Meshes/BistroExterior.cmesh`
- `Cerberus-Editor/Resources/Materials/Defaults/BistroExterior/` (132 `.cmaterial`)
- `Cerberus-Editor/Resources/Scenes/Bistro.cscene`

These paths are **already** excluded from git via `.gitignore` (the "Local test / source assets" block), so
nothing here needs a new `Assets/Meshes/External/Bistro/` entry — the existing exclusion already covers it.
I have not added `SOURCES.txt`/licence attribution yet; that's the one acquisition item still open (see
Open items below) since the task explicitly wants the NVIDIA ORCA URL, version, licence and attribution text
recorded, and that wasn't there.

## 1. Counts

| | Count | Source |
|---|---|---|
| Materials | **132** | `.cmaterial` files under `Materials/Defaults/BistroExterior/`, and `MaterialSlot: 0..131` in `BistroExterior.fbx.meta` |
| Scene entities | **3** (Sky Light, Directional Light, one `BistroExterior` MeshComponent) | `Bistro.cscene` |
| Source DDS textures | **622** | file glob under `Textures/` |
| — BaseColor | 203 (157 real 2048², 46 flat 16² dummies) | DDS header scan |
| — Normal | 203 (170 real 2048², 33 flat 1² dummies) | DDS header scan |
| — Specular | 201 (3 real 2048², 198 flat 16² dummies) | DDS header scan |
| — Emissive | 15 (7 real 2048², 8 flat 64² dummies) | DDS header scan |
| Acceleration-structure build entries (PathTracer BLAS/TLAS pass) | **≥ 1591** (validation errors numbered queries 0..1590) | live run, Vulkan validation log |

**Open question, not resolved by this report:** the mesh itself is one `MeshComponent` with 132 material
slots (`aiProcess_OptimizeMeshes` is on, see §4), so I expected the submesh count to be close to 132, but the
PathTracer's AS pass produced at least 1591 distinct query indices. I did not have time to instrument the
code to find out whether that's 1591 BLAS builds, 1591 TLAS instances, or something else — flagging it rather
than guessing. A one-line temporary log in `RenderScene`/the AS builder (submesh count, instance count,
total triangle/vertex count) would resolve this cheaply and should be the first thing done in Step 2 before
touching import code.

I could not get exact triangle/vertex counts without that same instrumentation (nothing in the current log
output prints them). What I do have: the combined vertex+index **staging upload for this one mesh was
198.62 MB** (`ResourceAllocator (GeometryPool Staging)` in the live run), against GeometryPool's fixed
256 MB (vertices) + 128 MB (indices) = 384 MB capacity — comfortable headroom (§6).

## 2. Texture formats and real channel packing

Checked by parsing DDS headers (all files) and, for the channel-packing question, by **decoding actual BC1/BC3
texel blocks by hand** (not assumed) from several representative textures.

- **Normal maps**: 100% **BC5** (`ATI2` FourCC), 2-channel. Matches the task's hint — Z must be reconstructed
  in-shader (`z = sqrt(saturate(1 - x² - y²))`). None are BC1/BC3/BC7.
- **BaseColor**: BC1 (`DXT1`, no alpha) for 142 textures, BC3 (`DXT5`, has alpha) for 61. The 61 with alpha
  line up with materials that are either foliage or glass (see §3) — alpha there is opacity, not a separate
  map.
- **Specular — decoded, not assumed.** Sampled many texels across `Vespa_Specular.dds`,
  `MASTER_Bistro_Main_Door_Specular.dds`, `Paris_TrafficSign_A_Specular.dds` (the 3 real painted 2048² maps)
  and several of the 198 flat 16×16 "dummy" ones:
  - **R channel: always exactly 0**, everywhere, in every file sampled. Not occlusion (that would make every
    surface fully occluded) — just unused/reserved.
  - **G channel: varies spatially** (69 distinct values sampled on the Vespa texture) and defaults to ~188/255
    (≈0.74) on materials with no real variation. Reads as **roughness**.
  - **B channel: low-cardinality, binary-ish** (e.g. 0 vs 255, or a handful of values) and differs between
    metal/non-metal regions of the same texture (e.g. the traffic sign's post vs. its painted face). Reads as
    **metalness**.
  - So: `Specular.rgb = (unused, roughness, metalness)`, no occlusion channel. This is a metal-roughness
    layout, not the CryEngine/Lumberyard spec-gloss-in-normal-alpha convention some older docs describe (that
    one needs a `_ddna` suffix and an alpha channel on the normal map; these normals are BC5 with no alpha, so
    that convention doesn't apply here).
- **Emissive**: 15 dedicated maps (not the BaseColor-alpha-as-emissive-mask guess I'd have made without
  checking), all BC1, no alpha needed since they're pure emissive colour.

## 3. Alpha / two-sided / emissive materials

Lumberyard's own authoring pipeline already encoded this in the **material names**, which Assimp preserved
and the importer used verbatim as `.cmaterial` filenames — no guessing needed:

- **6 materials end in `.DoubleSided`**: all foliage/ivy/tree-leaf materials (`Foliage_Bux_Hedges46`,
  `Foliage_Flowers`, `Foliage_Ivy_leaf_a`, `Foliage_Leaves`, two `Foliage_Linde_Tree_Large_*_Leaves`). Their
  BaseColor maps are all BC3 (real alpha) — these are the alpha-masked, two-sided materials Step 3 needs to
  handle.
- **1 material is named `..._MASKED`**: `MASTER_Glass_Dirty_MASKED`.
- **13 materials are named `..._BLENDSHADER`**: pavement variants with wet/leaf overlays, a couple of brick
  and plaster variants, `MASTER_Bronze_BLENDSHADER`. These look like genuine alpha-**blend** materials
  (translucency/overlay), not just alpha-test — which the T5 task explicitly puts **out of scope**
  ("transparencies (blend)"). Flagging now so Step 3 doesn't accidentally try to mask these: they should stay
  opaque-rendered (ignoring their blend intent) until a later task picks up blending.
- Everything else (111 materials) has no alpha/two-sided hint in its name and uses opaque BC1 BaseColor.

**Current importer does not yet use any of this.** I checked several `.cmaterial` files already on disk
(`Foliage_Leaves`, `Glass_Dirty_MASKED`, `LanternEmissive`, `Vespa`, `MASTER_Black_Metal`): every single one
has **the exact same hardcoded `Roughness: 0.55278641`**, `Metalness: 1`, and `RoughnessMapHandle: 0` —
regardless of whether the material is foliage, glass, metal, or an emissive lantern. `MetalnessMapHandle` is
0 for all but one material I checked (`MASTER_Black_Metal`, where it's oddly set equal to that material's own
`AlbedoMapHandle` — looks like an import bug, not intentional reuse). In short: **the Specular texture isn't
being read at all today**; every material gets the same scalar roughness/metalness regardless of its real
Specular map. This matches `MaterialProxy.h`'s `GPUMaterialData` already having no `AlphaMode`/`AlphaCutoff`/
`TwoSided` fields (confirmed by reading the struct — exactly as the task doc assumed), but goes a bit further:
roughness/metalness-from-texture isn't wired up either, so Step 2's material-mapping work has more to do than
"just" add alpha.

## 4. Import flags (current code, read directly from `MeshAsset.cpp`)

```cpp
aiProcess_CalcTangentSpace | aiProcess_Triangulate | aiProcess_SortByPType |
aiProcess_GenNormals | aiProcess_GenUVCoords | aiProcess_OptimizeMeshes |
aiProcess_JoinIdenticalVertices | aiProcess_ValidateDataStructure
```

Matches the task doc exactly. `aiProcess_OptimizeMeshes` is **on**, unconditionally — there is no import
option to turn it off yet (confirmed: no "optimize meshes" flag anywhere in `MeshAsset.cpp`, no per-asset
override in the `.fbx.meta`). This is the Step 2 "make it an import option, default off for Bistro" item.

## 5. Scale / units

- The raw FBX's `GlobalSettings::UnitScaleFactor = 100.0` (decoded directly from the FBX binary; confirmed via
  the adjacent `OriginalUnitScaleFactor = 1.0`), i.e. the content was authored in meters and carries a
  100×-to-centimeter conversion flag for DCC round-tripping.
- `MeshAsset.cpp` **applies no scale correction at all** at import time (confirmed: no `Scale`/`scale`/`unit`
  handling anywhere in the file). Whatever units Assimp's FBX importer hands back for vertex positions is
  what ends up in `GeometryPool`, unscaled.
- I could not pin down the exact resulting world-space size of the mesh without the same triangle/bounds
  instrumentation mentioned in §1 — the one `MeshComponent` entity in `Bistro.cscene` has `Scale: [1,1,1]` and
  a small translation (`[-0.66, -0.89, 2.13]`), which tells us nothing about the mesh's own vertex-space
  extents.
- Circumstantial evidence this is already a real problem: the previous session had to add a user-facing
  "Camera → Far Clip" control specifically because *"it's cutting off a large scene I have"* — before this
  Bistro inspection connected the dots. If Bistro's vertices are in Assimp's raw (likely centimeter-scale, per
  the FBX's own units) coordinates and nothing rescales them, a building that's actually ~30–40 m across would
  sit at ~3000–4000 engine units, which is exactly the kind of mismatch that forces the far plane up and makes
  the default camera speed feel wrong. Step 2's "apply correct unit scale at import time (not a one-off camera
  fix)" is very likely the real fix for that complaint, not just a Bistro nicety.

## 6. Capacity / footprint estimate against current limits

| Resource | Capacity | Bistro's share | Fits? |
|---|---|---|---|
| GeometryPool vertices | 256 MB (fixed, first-fit) | — (see §1, not isolated from indices in the one staging log line) | yes, comfortably — combined vertex+index staging for the whole mesh was 198.62 MB, well under the 384 MB total pool |
| GeometryPool indices | 128 MB (fixed, first-fit) | — | — |
| Bindless texture registry | 4096 (`engineTextureBudget` in `Device.cpp`) | 622 Bistro textures | yes, huge headroom (~3450 slots free even with other test scenes' textures already resident) |
| Material storage buffer | dynamically sized, no fixed cap | 132 materials | no concern |

## 7. Validation / correctness issue found (not Bistro-specific, but only surfaced at this scale)

Running the editor against `Bistro.cscene` (PathTracer, as the scene currently specifies) with the standard
validation layer produced **1592 validation errors**, not the "zero" the task's acceptance criteria wants:

- Root cause: `VUID-vkResetQueryPool-None-02665` — `vkResetQueryPool()` is called but the
  `hostQueryReset` device feature isn't enabled.
- Consequence: every subsequent `vkCmdWriteAccelerationStructuresPropertiesKHR` call (one per BLAS
  compaction-size query, numbered 0 through at least 1590 in the log) reports `UNASSIGNED-CoreValidation-
  DrawState-QueryNotReset`.

This is a **pre-existing RT/acceleration-structure bug**, unrelated to anything in the Bistro import itself —
it just never showed up before because earlier scenes didn't build anywhere near 1500+ BLAS/queries. Fixing
it (enabling `VkPhysicalDeviceHostQueryResetFeatures::hostQueryReset` at device creation, alongside the actual
reset call) is one line in `Device.cpp`'s feature chain, same pattern as T4's Vulkan 1.2 features merge — but
it touches RT device setup, which is outside this task's stated scope (Forward renderer + benchmark tooling),
so I'm reporting it rather than fixing it here. Worth its own tiny CL before or alongside T5 Step 5's "zero
validation errors" acceptance pass, since Bistro is exactly the scene that will keep triggering it.

No other validation errors or warnings appeared beyond the one pre-existing benign `InvalidRenderpass` warning
already known from T3/T4 testing.

## 8. Cold import timing (approximate)

Loading `Bistro.cscene` cold (no mesh-data cache exists at all today — only shaders get cached under
`Resources/Cache/Shader/`, confirmed by there being no equivalent mesh cache directory) took from scene-load
start (`11:57:00`, first `Deserializing scene` line) to the geometry upload finishing (`11:59:09`,
`GeometryPool Staging` allocation) — **≈ 2 minutes 9 seconds**, the bulk of which was Assimp parsing the
120 MB FBX and the per-material texture/shader setup that follows it, not GPU upload itself. This included
Debug-build overhead (assertions, no LTO) and this being the *first* load of the process, so a warm,
Release-build, non-first-process number would likely be noticeably faster — but there is currently no warm
path to compare against (see above), so Step 2's "measure cold vs warm" will need the AssetSerializer mesh
cache to exist first.

## Open items carried from Step 1, now resolved

1. ~~Add the one-off triangle/vertex/submesh/instance counters~~ — done, see §9. The 132-materials vs.
   ≥1591-AS-queries discrepancy is resolved: it's submeshes, not materials or TLAS instances (§9).
2. `SOURCES.txt`/licence attribution for the already-present Bistro assets — still not done; paperwork only,
   deferred since it doesn't block anything else in this task.
3. ~~Confirm actual world-space scale~~ — done, see §9.

---

## Step 2 — Robust import

All of this is implemented, built (Debug + Release, 0 warnings/errors), and validated against both Bistro
(Forward, live run, stable, zero validation errors) and the Sponza/DamagedHelmet/Cerberus-gun/Sphere/Cube
regression scene (`First Scene.cscene`, live run, stable, zero new errors). Every change below defaults to
exactly the prior behavior for anything that doesn't opt in by name or by its own file format/metadata, which
is how the regression scene stays byte-for-byte unaffected.

### 9. Unit scale — root cause found and fixed, resolves the Step 1 open question

Added a one-off counters probe to `MeshSource`'s constructor (kept as a permanent `NV_MESH_LOG`, not removed)
and used it to settle Step 1's two open questions directly:

- **Raw vertex bounds**: `min=(-3903.6, -6030.1, -472.6) max=(6956.4, 5496.1, 2720.8)`, i.e. roughly
  **108.6 m × 115.3 m × 31.9 m** if read as centimeters — a very plausible size for "one Parisian city block",
  confirming the raw FBX data is in centimeters.
- **Why nothing was scaling it**: `MeshSource` now reads the FBX's own `UnitScaleFactor` metadata (Assimp
  exposes it on `aiScene::mMetaData`) and bakes `1/UnitScaleFactor` into the root node transform that
  `TraverseNodes` seeds with (`Cerberus/src/Asset/Assets/MeshAsset.cpp`), instead of leaving every submesh's
  accumulated transform at raw-FBX-unit scale. While building this I found **the actual reason the unscaled
  behavior existed at all**: Assimp's FBX importer stores `UnitScaleFactor` as a **`float`**
  (`fbx_simple_property(UnitScaleFactor, float, 1)` in the vendored `FBXDocument.h`), but both my first attempt
  and — more interestingly — **Assimp's own internal up-axis/scale correction**
  (`FBXConverter.cpp::correctRootTransform`) query it as a **`double`**. `aiMetadata::Get<T>` checks the
  stored type exactly, so asking for the wrong one silently returns `false` with no error — meaning Assimp's
  own built-in unit correction has never actually applied for this vendored Assimp version, for any FBX, not
  just Bistro. Reading it as `float` fixed it.
- **Why this is almost certainly the real cause of the far-clip complaint**: with the fix in, Bistro renders at
  a sane ~100 m scale, well inside the *default* camera's `far = 1000`. The "Camera → Far Clip" control added
  earlier this session is no longer needed for Bistro specifically, though it stays as a generally useful
  override for scenes that genuinely exceed 1000 units.
- **Confirmed zero effect on every non-FBX-with-this-metadata asset**: live-logged `UnitScaleFactor` reads for
  the regression scene — `Sponza.gltf`: not present (glTF has no such key) → scale 1. `DamagedHelmet.gltf`:
  not present → scale 1. `CerberusMaterials.fbx` (the gun): present, **value = 1** → scale 1 (no-op; this FBX
  was already authored 1:1). `Sphere.fbx`/`Cube.fbx` (engine defaults): present, value = 1 → scale 1. Nothing
  about any of them changed.

### 10. OptimizeMeshes — now an import option, off for Bistro, but makes no difference for it

Added `MeshSourceProperties::OptimizeMeshes` (default `true`, preserving current behavior for every mesh
imported before this existed — absent `.meta` key reads back as `true`), threaded it into
`GetMeshImportFlags()`, and persisted it in each mesh's `.fbx.meta`/`.gltf.meta` (`AssetSerializer.cpp`).
Set to `false` for `BistroExterior.fbx.meta` specifically.

**Measured, not assumed** (same `NV_MESH_LOG` counters probe, one live run per setting):

| OptimizeMeshes | Submeshes | Vertices | Indices |
|---|---|---|---|
| `true` (prior default) | 1,591 | 3,112,143 | 2,832,120 |
| `false` (Bistro's new setting) | 1,591 | 3,112,143 | 2,832,120 |

**Identical.** `aiProcess_OptimizeMeshes` turns out to be a complete no-op for this FBX — there was nothing to
merge, meaning every one of Bistro's 1,591 original mesh nodes already uses a materials/skinning combination
distinct enough that Assimp's merge pass never combines any two of them. This also resolves Step 1's "132
materials vs. ≥1,591 AS-build-queries" mystery: **1,591 is the submesh count**, confirmed exactly (not TLAS
instances, not something material-count-related) — the PathTracer's acceleration-structure pass builds one
query per submesh, both with the flag on and off, which is additional confirmation the counts above are right.
Left `OptimizeMeshes: false` set for Bistro anyway: no downside since it's a no-op here, and it's the more
correct setting of intent for whenever per-object culling needs that granularity later.

### 11. Specular → roughness/metalness, emissive textures, BC5 normals

- **`GPUMaterialData` gained two `uvec4`/`vec4` fields** (`TextureIndices2.x` = emissive texture index,
  `Flags.x`/`Flags.y` = packed-specular / reconstruct-normal-Z) in `MaterialProxy.h`, mirrored in both GLSL
  copies of the struct (`ForwardPBR.frag`, `RayTracing/Material.glsl`). Every existing serialized `.cmaterial`
  defaults both new flags to `false` and the new texture slot to invalid, so every material that existed
  before this change samples exactly the way it did before.
- **Specular packing** (§2's decoded R=unused/G=roughness/B=metalness): `MeshAsset.cpp`'s texture classifier
  now recognizes `*_specular*`/`*_spec*` filenames, assigns that texture to **both** the Metalness and
  Roughness slots (only if the material doesn't already have dedicated maps for either), and sets
  `PackedSpecularGB`. `ForwardPBR.frag` and `Raytrace_rchit.rchit` both branch on that flag: packed reads
  `.g`/`.b`, everything else keeps reading `.r` exactly as before. This is the fix for the "Specular texture
  isn't being read at all" finding from §3 — Bistro's 132 materials now actually use their Specular maps.
- **Emissive textures**: added a real `EmissiveMap` slot end-to-end (`MaterialAsset`, `.cmaterial`
  serialization, `RenderScene::BuildMaterialGPUData`, both shaders) instead of the previous
  albedo-tinted-by-a-scalar approximation. Classifier recognizes `*emissive*`/`*emission*` filenames. Forward
  falls back to tinting albedo (its old behavior) when no emissive texture is set; the RT hit shader falls
  back to its old flat-white-times-scalar behavior the same way — both are no-ops for every material that
  doesn't have a dedicated emissive map, which per §3 is everything except Bistro's 15 lantern/sign materials
  and (harmlessly, since its `Emission` scalar is `0`) one `DamagedHelmet` material that happened to also match
  the new `*emissive*` filename rule.
- **BC5 normal-Z reconstruction**: after a material's normal map is resolved, `MeshAsset.cpp` loads the
  already-imported `TextureAsset` and checks its real GPU format (not the source filename) for
  `BC5_UNORM`/`BC5_SNORM`, setting `ReconstructNormalZ` only then. Both shaders reconstruct
  `z = sqrt(saturate(1 - x² - y²))` from the sampled XY when set, instead of reading a third channel BC5
  doesn't have (previously: undefined/zero, i.e. visibly wrong normals). Sponza/DamagedHelmet/the gun use
  BC1/BC3/JPEG-backed normal maps with a real stored Z, so this flag never triggers for them and their shading
  is bit-for-bit the same codepath as before.

### 12. Missing textures and scale-triggered regressions

- **Missing-texture fallback**: already handled by existing code (`importMaterialTexture` in `MeshAsset.cpp`
  logs a warning and returns a null handle rather than crashing) — verified by reading it in Step 1, no change
  needed.
- **Regression check**: live run of `First Scene.cscene` (Sponza + DamagedHelmet + Cerberus gun + 2 spheres +
  cube) after all of the above — zero crashes, zero new validation errors, and the one material diff
  (`DamagedHelmet_Material_MR`) is the harmless `Emission: 0` emissive-map case from §11.
- **Camera speed/near/far**: deliberately left untouched. With the unit-scale fix in, Bistro sits at the
  engine's existing default scale assumptions (§9), so the task's "adjust camera speed and near/far" ask turned
  out to already be satisfied by fixing the real bug instead of needing a separate camera tweak.

### 13. Cold/warm load timing — still only cold exists

No change from §8: there is still no mesh-data cache (only shaders cache). Building one is a real feature
(serialize `MeshSource`'s parsed vertex/index/submesh/material-handle data so a second load skips re-running
Assimp entirely) — out of scope for this CL; flagging again rather than building it speculatively. Cold import
with all of the Step 2 changes in is still in the same ~2-minute ballpark as §8's baseline; the changes
themselves (classification, scale, flag-setting) are O(materials)/O(nodes), not O(vertices), so they don't
materially add to it.

### Validation summary for Step 2

- Debug + Release, all three projects (Cerberus, Cerberus-Editor, Cerberus-Runtime): 0 warnings, 0 errors.
- Bistro, Forward renderer, live run: reached a stable frame loop, zero validation errors (the §7
  host-query-reset bug is PathTracer/acceleration-structure-specific and doesn't reproduce on the Forward
  path at all).
- Bistro, PathTracer, live run: same §7 bug, same bound (query 1590 either way) — confirmed unrelated to any
  Step 2 change, since it reproduces identically before and after.
- `First Scene.cscene` (Sponza/DamagedHelmet/Cerberus/Sphere/Cube), Forward renderer, live run: zero errors,
  zero unintended material/scale changes.

## Remaining for Step 3+

Update: Step 3 implementation and its validation record are now documented in
[bistro-alpha-masking.md](bistro-alpha-masking.md). The original Step 1–2 results
above are retained as historical observations. Step 4 remains outside this change.

Step 2's scope is done. Step 3 (alpha-masked materials: `AlphaMode`/`AlphaCutoff`/`TwoSided`, render buckets,
masked PreDepth/Shadow/Forward pipelines) hasn't been started. `SOURCES.txt`/licence attribution (open item 2
above) is still outstanding paperwork. A real mesh-data cache (§13) remains an identified gap, not yet built.

Stopping here per the task's Step 1 instruction ("stop and show me the report before step 2"). Nothing else
was changed — `EditorLayer.cpp`'s scene pointer was temporarily switched to `Bistro.cscene` for this
inspection and has been switched back to `First Scene.cscene`.
