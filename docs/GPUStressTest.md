# GPUStressTest: generation, use and technical report

## Delivered scene

`GPUStressTest` is a deterministic Forward-only architectural stress environment assembled from the existing beveled Cube, Cylinder, Torus and Cone assets. It contains tiled stone platforms, columns and capitals, portal rings, entablatures, roof finials, balustrades and large occluding walls. These are repeated modular structures with open passages, silhouettes and multiple depth layers, rather than an unstructured cube grid. Portal rings are complete tori, not newly modeled arch meshes.

The scene uses exactly one directional light (intensity 3) and the existing `pink_sunrise_4k.hdr` SkyLight (intensity 0.6, SampleLOD 1). Forward creates its normal shadow cascades when a directional light exists; there is no separate serialized shadow-enable field in the inspected system. No rendering feature was added.

The isolated checkout is `E:\Dev\Cerberus-Engine\.worktrees\benchmark-tooling-stress`, branch `codex/benchmark-tooling-stress`. No renderer, shader, RHI, RenderGraph, T2 implementation, registry or existing scene was modified. Generated assets can be copied into the active editor project's new `Resources/Scenes/GPUStressTest` directory without replacing existing files or switching its branch.

## Files

| Path relative to this worktree | Purpose |
| --- | --- |
| `tools/gpu-stress/generate.py` | Standalone Python 3.10+ scene generator, standard library only. |
| `tools/gpu-stress/presets.json` | Editable instance targets: 1,000 / 10,000 / 50,000 / 100,000. |
| `tools/gpu-stress/assets.json` | Counts, imported bounds and SHA256 fingerprints of the four source FBX files. |
| `tools/gpu-stress/inspect_assets.py` | Optional inventory refresh using the bundled Assimp 4.x DLL and the engine's import flags. |
| `tools/gpu-stress/validate.py` | Independent references, counts, determinism and off-frustum diagnostic checks. |
| `tools/gpu-stress/validate_yaml.cpp` | Offline parse/schema validation with the engine's actual yaml-cpp library. |
| `tools/gpu-stress/build_validator.cmd` | Build that offline validator from a VS x64 tools prompt. |
| `Cerberus-Editor/Resources/Scenes/GPUStressTest/GPUStressTest.cscene` | Ready-to-load Low scene; committed. |
| Same directory, `GPUStressTest_{Medium,High,Extreme}.cscene` | Larger generated scenes, present locally after generation and intentionally ignored by Git. |
| Same directory, `*.benchmark.yaml` | Three fixed T2 cameras for each preset; committed. |
| Same directory, `*.manifest.json` | Exact instance/triangle accounting, source basis, hashes and memory estimates; committed. |

Scene files and camera sidecars use JSON syntax, a YAML-compatible flow format accepted by the existing yaml-cpp loader. UUIDs and asset handles are integer literals and must not be round-tripped through tools that store numbers as IEEE double. The generator never edits `AssetRegistry.cregistry`; the existing AssetManager scans new scene files at startup and assigns their scene handles.

## Generate

Run from the worktree root in PowerShell:

```powershell
py -3 tools/gpu-stress/generate.py
py -3 tools/gpu-stress/generate.py --preset Medium
py -3 tools/gpu-stress/generate.py --preset all --allow-large
```

Default generation writes only Low. Above 10,000 instances, `--allow-large` is required; it permits file generation, not automatic GPU execution. Custom target:

```powershell
py -3 tools/gpu-stress/generate.py --preset Low --instances 2000 --output intermediate/CustomStress
```

Use a separate output folder for custom counts to preserve the published Low capture identity and sidecar. A non-multiple of 20 truncates the final module deterministically.

To generate directly into another editor checkout without changing its source, while resolving its asset handles and checking its source fingerprints:

```powershell
py -3 tools/gpu-stress/generate.py --repo E:\Dev\Cerberus-Engine --preset Low --output E:\Dev\Cerberus-Engine\Cerberus-Editor\Resources\Scenes\GPUStressTest
```

Close that editor first so its asset scan can discover the new scene on restart. This command creates/replaces only files named by this generator in the specified output folder. It was not executed against the active checkout in this session.

## Load and capture

1. Start with Low. Keep the generated scene and its same-stem `.benchmark.yaml` together under the editor's `Resources/Scenes/GPUStressTest` folder.
2. Start/restart the editor after adding assets. AssetManager loads the registry and scans the resource directory. Do not manually merge the asset registry.
3. Choose **File > Open Scene** (`Ctrl+O`) and open `GPUStressTest.cscene`. Alternatively use the Content Browser. The scene requests **Forward** and contains an initial overview camera.
4. Confirm Forward is active. Inspect the three districts, positive illumination, sky and contact/cast shadows. The expected Low count is 1,000 mesh instances and 755,144 scene triangles.
5. Open **Window > Benchmark** (the Window menu's Benchmark panel toggle). Its saved camera list should show `A Geometry Throughput`, `B Draw Call Overhead`, `C Visibility Occlusion`. Use each camera's `Go to` button to inspect it.
6. Select a fixed resolution, initially 1080p, and set warm-up/sample frames, e.g. 60/300. Do not edit or save over the supplied camera poses during comparisons.
7. Choose **Run benchmark**. Existing T2 iterates the saved cameras, holds each pose and exports a CSV to `Benchmarks/` at the detected repository root. Successful CSV export still requires runtime verification on your machine.
8. Preserve the CSV in a uniquely named run directory, record renderer options/submission mode manually, and refresh the Power BI report described in `docs/BenchmarkVisualizationGuide.md`.
9. Increase to Medium only after confirming runtime behavior and memory headroom. High/Extreme are experimental generated workloads, not certified interactive presets. Keep PathTracer disabled for these runs.

The default editor still starts its existing First Scene; this work does not replace that scene or change startup behavior. A standalone worktree must have its normal dependencies and local assets initialized before it can launch the editor.

## Workloads and fixed cameras

The layout has three spatially separated districts. Modules are 18 units apart. District spacing grows with the preset, keeping them separated as counts increase. UUIDs, transforms, geometry selection and cameras are deterministic; there is no random seed or wall-clock input.

| Camera | Intended workload | Arrangement |
| --- | --- | --- |
| A Geometry Throughput | Many visible architectural surfaces and high vertex load | Elevated wide overview of columns, rings, finials and repeated courtyards. |
| B Draw Call Overhead | Many individually submitted small architectural pieces | Street-level view through balustrades, narrow posts and portal layers; most columns/finials in B use the existing beveled Cube to reduce geometry per object. |
| C Visibility Occlusion | Future frustum and occlusion culling | Street view toward an 8-unit-high wall with further geometry behind it; other districts and side streets remain outside its frustum. |

Camera A scales with environment extent; B/C remain near street level. All have fixed FOV, near 0.2 and far 6000, encoded in the sidecars. Each preset is its own workload: compare renderer revisions using the same preset and sidecar hash, not Low versus Extreme as if camera/content were identical.

These are views of one combined scene, not three independently submitted object lists. With the current uncullled renderer **every camera still submits all instances** to PreDepth, each active shadow cascade and Forward. Camera choice changes screen coverage, overdraw and visibility, not the CPU draw count. This is useful as a future culling baseline but does not produce a pure isolated vertex-only or submission-only microbenchmark. No visibility or occlusion savings are claimed.

## Counts and memory

Imported assets were inspected through the existing Release `assimp-vc141-mt.dll` using the same import flags as `MeshAsset.cpp`:

| Mesh | Imported vertices | Triangles | Submeshes |
| --- | ---: | ---: | ---: |
| Cube (beveled) | 128 | 156 | 1 |
| Cylinder | 1,357 | 1,408 | 1 |
| Torus | 1,885 | 3,584 | 1 |
| Cone | 1,869 | 1,536 | 1 |

The generator checks source SHA256 and mesh-to-source registry relationships before trusting this inventory. `inspect_assets.py` explicitly supports the bundled Assimp **4.x DLL ABI**, which differs from newer source headers in the repository; do not pass an arbitrary newer DLL. A rebuilt engine with different importer behavior must confirm its displayed counts and refresh the inventory if necessary.

| Preset | Mesh instances | Scene triangles | Triangles submitted across six geometry passes | T4 instance+args, three frames |
| --- | ---: | ---: | ---: | ---: |
| Low | 1,000 | 755,144 | 4,530,864 | 540,000 B |
| Medium | 10,000 | 7,574,744 | 45,448,464 | 5,400,000 B |
| High | 50,000 | 37,889,256 | 227,335,536 | 27,000,000 B |
| Extreme | 100,000 | 75,770,744 | 454,624,464 | 54,000,000 B |

Each preset also has two light entities. There is one submesh per reused mesh, so serialized mesh entity count equals expected raster instance count. Scene triangles count each instance once; the six-pass column assumes PreDepth + four shadow cascades + Forward, with no culling. It describes submitted primitives, not a measurement of actual shader invocations or rasterized triangles.

Unique uploaded geometry is only **373,592 bytes**: 293,384 vertex bytes at 56 bytes/vertex and 80,208 index bytes. `RenderScene` caches mesh sources and reuses those four source proxies across all instances. It does not allocate one copy of geometry per entity.

The inspected T3 GeometryPool reserves **384 MiB** independent of actual use (256 MiB vertices + 128 MiB indices). T4 estimates use 160 bytes of instance data plus 20 bytes of arguments per instance per frame. They describe the concurrently developed T4 layout and are not additional features implemented by this work. On a pre-T4 checkout those buffers do not exist.

These numbers are not total VRAM or RAM: render targets, four 4096 shadow maps, HDR environment baking/cubemaps, materials, driver allocations, ECS entities, material tables, DrawStream, CPU vectors, YAML parsing and allocator overhead are additional. No OOM-free guarantee can be made from geometry sharing alone. Extreme is also capable of very long frames/TDR despite modest incremental geometry memory. Load one preset at a time and monitor GPU memory and frame duration; do not launch High/Extreme automatically.

## Implementation investigation

- `Cerberus/src/Asset/AssetSerializer.cpp`: SceneSerializer reads scene renderer, EditorCamera, UUIDs, relationships, transforms, MeshComponent/material tables and both lights. MeshSerializer resolves `.cmesh` to registered MeshSource. The generated fields match these readers.
- `Cerberus/src/Asset/AssetManager.cpp`: startup registry load plus recursive resource scan allows standalone new scene files without changing a shared registry.
- `Cerberus/src/Renderer/Scene/RenderScene.cpp`: existing mesh-source cache shares geometry; whole-mesh instances compose imported node transforms. Generated `SubmeshIndex = -1` preserves that behavior.
- `Cerberus/src/Asset/Assets/MeshAsset.cpp`: Assimp flags and triangle/vertex collection are the basis of inventory accounting.
- `Cerberus/src/Core/FlyingCamera.cpp`: yaw/pitch use degrees and forward is `(cos(yaw)cos(pitch), sin(pitch), sin(yaw)cos(pitch))`. Sidecar camera poses are calculated from explicit look targets using that convention.
- `Cerberus-Editor/src/Panels/BenchmarkPanel.cpp`: sidecar replaces `.cscene` with `.benchmark.yaml`; reads `Cameras`, Name/Position/Yaw/Pitch/FOV/Near/Far. No sidecar schema changes were needed.
- Existing `First Scene.cscene` contains Sponza, materials and lights; local `Bistro.cscene` references the external Bistro mesh/material set. Heavy source sets are ignored by Git. Reusing the tracked defaults keeps this tool portable across initialized checkouts without requiring those external datasets.

## Validation executed

- Imported all four source meshes successfully with the existing Assimp DLL; verified one submesh each, counts and imported bounds. Source fingerprints are stored in `assets.json`.
- Generated all four presets successfully, including the 100,000-instance scene body.
- Ran `validate.py` against all four: unique UUIDs, exact counts/triangle sums, mesh/source/material/environment references, finite positive scales, one of each light, three cameras, and byte-identical regeneration passed.
- Offline camera C diagnostic found 769 / 7,455 / 37,151 / 73,939 conservative horizontal-frustum-outside candidates for Low / Medium / High / Extreme at 16:9. This establishes off-frustum content, not occlusion correctness or runtime culling.
- Built the standalone C++ validator with MSVC and the existing Release yaml-cpp library. All four scene bodies and camera sidecars passed the native parser/schema checks (including 100,000 mesh instances and three cameras for Extreme). This validates the JSON/YAML representation and typed fields, not full Scene construction or GPU execution.
- Attempted isolated project generation using Premake: **failed because `Cerberus/vendor/GLFW` is an uninitialized submodule in the new worktree**. No dependencies were initialized/downloaded, and the active checkout's projects/binaries were not rebuilt or launched.

**Runtime validation remains outstanding:** full scene loading/entity creation, rendered geometry, directional/environment lighting, shadows, camera navigation, T2 capture, Vulkan validation, measured frame time/VRAM and existing-scene regression. No screenshots, performance gains, engine-build success or successful runtime capture are claimed.

## Re-run offline checks

```powershell
py -3 tools/gpu-stress/validate.py
py -3 tools/gpu-stress/inspect_assets.py --repo E:\Dev\Cerberus-Engine --dll E:\Dev\Cerberus-Engine\bin\Release-windows-x86_64\Cerberus-Editor\assimp-vc141-mt.dll
```

The second command refreshes only `tools/gpu-stress/assets.json`; review counts/bounds before regenerating scenes. In a **VS 2022 x64 Native Tools Command Prompt**, build the native validator with a checkout containing built yaml-cpp (read-only dependency):

```bat
tools\gpu-stress\build_validator.cmd E:\Dev\Cerberus-Engine
intermediate\stress-validation\validate_yaml.exe Cerberus-Editor\Resources\Scenes\GPUStressTest
```

For full runtime validation, initialize/build the isolated checkout using the repository's normal setup, or generate the scene into an already initialized checkout after coordinating its editor session. This task does not merge or modify the ongoing GPU-driven implementation.
