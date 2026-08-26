# Cerberus Renderer Architecture

Last updated: 2026-08-26

This document defines the current renderer ownership model, the command-recording architecture, and the RenderGraph that now orders passes and derives barriers for both scene renderers.

The immediate priority is stability: the path tracer, modern forward renderer, editor, scene data, materials, environment lighting, and GPU object lifetime must remain reliable while pass boundaries are made explicit.

## Architectural constraints

1. `Renderer` remains a small global facade and frame-lifecycle coordinator.
2. Scene data, render passes, resource ownership, command recording, and command submission must have separate owners.
3. No two wrappers may believe they own the same `VkCommandBuffer` or native resource.
4. `GraphicsCommandList`, `ComputeCommandList`, `TransferCommandList`, and `RayTracingCommandList` are typed, non-owning recording views. They do not allocate, submit, or destroy command buffers.
5. Pipeline objects own pipeline state only. They must not own active command-buffer state or perform submission.
6. The current path tracer and editor must remain operational throughout the migration.
7. `DrawStreamBuilder` is retained as the compact raster draw encoding path.
8. True asynchronous compute is deferred until resource dependencies can be scheduled explicitly, ideally by the RenderGraph.
9. Renderer-owned `OLD_RENDERER`, dead `#if 0`, obsolete APIs, and commented implementations must not survive Phase 2C.4A.
10. Third-party implementation guards, such as those inside ImGuizmo, are outside the legacy-renderer cleanup scope.

## System responsibility boundaries

### Renderer

`Renderer` is the public facade for process-wide rendering services. It coordinates:

- initialization and shutdown;
- `BeginFrame` and `EndFrame`;
- current frame state;
- renderer-wide service access;
- execution of the CPU render queue;
- deferred native-resource destruction;
- access to persistent and transient command infrastructure during migration.

`Renderer` must not contain:

- scene mesh/material registries;
- BLAS/TLAS ownership;
- pass-specific descriptor binding logic;
- raster draw implementations;
- compute dispatch implementations;
- environment-map baking implementation;
- Dear ImGui backend implementation;
- active pipeline state.

### RendererContext and Device

`RendererContext` and `Device` own Vulkan instance/device-level state:

- `VkInstance`, `VkPhysicalDevice`, and `VkDevice`;
- graphics, compute, present, and transfer queues;
- queue-family information;
- device limits and ray-tracing capabilities;
- allocator/VMA integration;
- backend extension entry points.

Higher-level systems may query these services but must not duplicate their ownership.

### RendererResources

`RendererResources` owns process-wide default renderer resources:

- white and black fallback textures;
- fallback cubemaps/environment;
- default PBR material;
- BRDF LUT;
- default samplers where applicable;
- global `ShaderLibrary`.

`Renderer` coordinates its lifetime but does not store these concrete resources directly in unrelated frame state.

### ResourceManager

`ResourceManager` owns engine wrappers and handle pools. It creates and destroys GPU resource wrappers such as buffers, images, image views, pipelines, bind groups, and layouts.

When a wrapper owns Vulkan/VMA resources, destruction extracts the native handles and submits only those native values to `DeferredResourceDeletionQueue`.

`ResourceManager` does not own native descriptor pools or descriptor-layout cache state.

### DescriptorManager

`DescriptorManager` is the single owner of:

- persistent descriptor pools;
- transient descriptor pools;
- descriptor-set layout caching;
- descriptor allocation lifetime policy.

Persistent pools are created with `VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT`.
`BindGroup` returns its persistent sets through `DescriptorManager`; the actual
`vkFreeDescriptorSets` call follows the deferred-deletion frame policy. This makes
repeated scene-renderer switching bounded instead of leaking descriptor capacity.
Transient sets are never freed individually and are reclaimed by their frame-pool reset.

`ResourceManager`, bind-group wrappers, and shader reflection request descriptor services through it without duplicating allocator ownership.

### DeferredResourceDeletionQueue

`DeferredResourceDeletionQueue` owns delayed native GPU destruction. Deferred callbacks capture native Vulkan/VMA handles by value.

They must not capture:

- engine resource wrappers;
- `ResourceManager` handles that may be recycled;
- `this`;
- references to mutable engine state.

### RenderQueueManager

`RenderQueueManager` owns CPU-side render command execution. It does not own GPU resources, descriptor lifetime, or deferred destruction.

`RenderQueue::Execute()` is reentrant: it is valid to call it again from inside
a command it is currently running. This matters because `ImGuiRenderer::Render()`
submits the entire ImGui frame (`NewFrame` -> `Application::OnImGuiRender` ->
`ImGui::Render`/`EndFrame`) as a single queued command, and arbitrary panel
code running inside `OnImGuiRender` — a lazily-loaded asset resolving, for
example — can need GPU setup work to complete immediately rather than waiting
for the frame's own flush (`EnvironmentMapBaker::Bake()` forcing its
just-submitted `ComputePipeline::Invalidate()` to run now is the concrete case
that surfaced this). The queue uses a persistent read cursor and an
execute-depth counter rather than always restarting from the buffer's start
and rewinding it to empty when done: a nested call shares the outer call's
exact read position, so it can never re-run a command the outer call already
executed, and only the outermost call is allowed to rewind the buffer, and
only once every command — including any a nested call appended — has actually
been read. The underlying command buffer stays one fixed-size allocation,
never reallocated mid-frame; a command's captured closure lives at a fixed
address for the duration of its own execution, so nothing that runs from
inside it can invalidate that address out from under it.

### ImGuiRenderer

`ImGuiRenderer` owns Dear ImGui backend state:

- ImGui descriptor pool;
- backend initialization and shutdown;
- ImGui command recording;
- swapchain integration.

The engine-side ImGui texture-ID cache is rebuilt once per ImGui frame. Those
texture descriptor sets therefore use the current frame's transient descriptor
pool, not the persistent allocator. The frame pool is reset only after its fence
has completed. Allocating these per-frame IDs as persistent resources causes an
unbounded descriptor leak and eventually `VK_ERROR_OUT_OF_POOL_MEMORY`.

### Editor viewport integration

Editor layout and viewport presentation state remain owned by `EditorLayer`, not
the renderer. The F11 maximize mode renders the existing scene output through a
temporary borderless ImGui viewport window and temporarily skips the dockspace
panels. The original docked `Viewport` window is never undocked or mutated, so
returning from maximize mode restores the previous editor layout automatically.

The renderer only receives the resulting viewport dimensions through the
existing `SceneRenderer::SetViewportSize` API. It has no editor-fullscreen or
docking state.

`Renderer` only coordinates its lifetime.

### Active scene-renderer ownership and switching

`EditorLayer` owns exactly one `SceneRenderer` through `Scope<SceneRenderer>`.
The active implementation is selected per `Scene` and serialized as `Forward` or
`PathTracer`. Shared ownership is intentionally forbidden here: the inactive
renderer must not remain alive through an editor callback or another subsystem.

Renderer changes requested by ImGui are deferred to `Layer::OnFrameEnd`, after
the frame's swapchain command buffer has been submitted and presented. This avoids
destroying an image view or descriptor already recorded by ImGui. At that lifecycle
boundary the editor:

1. drains queued CPU render work and waits for the device;
2. destroys the active `SceneRenderer` and its `RenderScene`;
3. flushes deferred native deletion;
4. resets renderer-specific mesh registration flags in `Scene`;
5. constructs only the selected renderer at the current viewport size.

The blocking wait is an intentional one-time editor switch cost. It is never part
of normal per-frame rendering. This guarantees that the inactive renderer retains
no output images, pipelines, bind groups, acceleration structures, scene proxies,
or renderer-specific shader modules.

## Scene and material boundaries

### Scene

`Scene` is the logical editor/game representation. It owns entities, components, transforms, asset references, and serialized state. It does not own Vulkan objects or acceleration structures.

### RenderScene

`RenderScene` is the renderer-facing representation of a `Scene`. It owns:

- mesh source registrations;
- mesh instance proxies;
- material proxies;
- render-object identity;
- scene-local GPU buffers;
- dirty state;
- BLAS/TLAS data required by ray tracing.

It bridges logical scene data and render passes without performing asset serialization or global renderer lifecycle work.

`RegisterMesh`'s optional `submeshIndex` filters registration down to one
submesh instead of every submesh in the mesh — the editor's "split a
multi-submesh mesh into per-submesh entities" feature uses this so each
resulting entity independently registers, transforms, and (via `MeshComponent
::SubmeshIndex`) reselects only its own submesh. A filtered instance's
`MeshInstanceProxy::WorldTransform` is the caller's transform alone: the
entity's own `TransformComponent` already bakes in that submesh's node
`Transform` at creation time (see `EditorLayer::CreateSplitMeshEntitiesFromDrop`),
so reapplying it here would double it up. `RegisterMesh` and `UpdateMeshTransform`
must therefore agree on whether a given `RenderObjectID` was filtered —
`MeshRegister::RegisteredMeshInstance::SubmeshFiltered` records that decision
once at registration time and both call sites read it, rather than each
re-deriving it independently and risking disagreement (this was a real bug:
`UpdateMeshTransform` used to always reapply the submesh `Transform`,
double-applying it and desyncing a split entity's render position from its
`TransformComponent` the moment the entity moved after creation).

`GetTotalTriangleCount()` sums each registered instance's own submesh
`IndexCount` once, independent of how many raster passes actually draw it.
This is deliberately not derived from `RendererFrameStatistics::DrawCallCount`
or a per-draw triangle tally in `GraphicsCommandList` — the Forward renderer
draws every instance multiple times a frame (PreDepth, each shadow cascade,
Forward geometry), so a draw-time sum would count scene geometry once per
pass instead of once per instance and report a number with no fixed
relationship to actual scene complexity. See "Statistics policy" below for
where the per-draw counters this deliberately does not use are reset and
incremented.

### Material ownership and render proxies

- `MaterialAsset` owns only serializable CPU values, a display name, and texture asset handles. It is renderer-agnostic and retains no shader, uniform buffer, or bind group.
- `RenderScene` translates a `MaterialAsset` into `GPUMaterialData`; each concrete renderer owns the shader and descriptor implementation that evaluates it.
- `MeshSource` owns imported material slots; `Submesh::MaterialIndex` indexes those slots.
- A mesh instance's `MaterialTable` may override any source material slot.
- `RenderScene` resolves the effective material per registered submesh and creates a scene-local `MaterialProxy`.
- `MeshInstanceProxy::MaterialProxyIndex` is a render-side index, not an asset pointer or raster bind group.
- `MaterialProxy::GPUData` is uploaded to the scene-local material buffer.
- Multiple actors may reference the same `MeshSource`; actor identity and transform belong to separate mesh-instance proxies.
- The TLAS custom instance index maps a hit to `RayTracingInstanceData`, which maps to the correct mesh instance, geometry, and GPU material index.
- Closest-hit reconstructs surface data; path integration and BSDF sampling remain in raygen.
- Material texture indices address one scene-local material texture table shared by raster and RT consumers. Every unused entry receives a valid fallback descriptor.

### Texture and environment ownership

- `TextureAsset` is decoded CPU/file data plus import metadata.
- `Texture2D` and `TextureCube` are GPU resources owned by `ResourceManager`.
- Asset-backed texture creation receives an explicit color space instead of mutating the source asset.
- The environment-map asset owns its source environment, filtered radiance cubemap, and irradiance cubemap handles.
- `EnvironmentMapBaker` converts equirectangular HDR data, builds and filters radiance mips, and generates irradiance through compute work.
- `Renderer::CreateEnvironmentMap` remains as the compatibility facade used by asset loading, but it only builds a bake specification and delegates to the baker.
- The baker is an isolated service, not a frame render pass. Baking is setup/asset processing work that internally records transient compute commands and may run outside normal frame-pass execution.

## Scene renderer and pass boundaries

`SceneRenderer` orchestrates rendering for one scene and view. Concrete renderers consume `RenderScene`, prepare view/output state, and execute typed passes.

`PathTracerSceneRenderer` owns the path-tracing view/output state, path-tracing
settings and accumulation state, and requests acceleration-structure preparation
from `RenderScene` before each frame. Its per-frame GPU work is a single logical
pass — one `RayTracingCommandList::TraceRays` dispatch into the accumulation
image — registered as one `RenderGraph` ray-tracing pass (see "RenderGraph"
below). TLAS build stays outside the graph: it submits through
`TransientCommandExecutor` on its own command buffer, the same category as
`EnvironmentMapBaker` (setup/asset-processing work, not a frame render pass).

The forward renderer implements the full intended pipeline:

```text
ForwardSceneRenderer
    Prepare frame data (camera/lighting/shadow/material buffers, draw stream)
    PreDepth                 -- writes SceneDepth
    Shadow x N cascades      -- one graph pass per cascade, each its own target
    ForwardOpaque             -- Sky + Forward geometry, one shared Dynamic Rendering scope
    Composite                 -- ACES tonemap + exposure + gamma -> LDR output
```

- owns SceneColor, SceneDepth, CompositeOutput, and one shadow-map render target per cascade directly (Dynamic Rendering has no framebuffer object to hold them instead);
- consumes `RenderScene` mesh instances and material proxies through one `DrawStreamBuilder`-encoded stream, replayed by PreDepth, every shadow cascade, and Forward geometry;
- evaluates normal, albedo, roughness, metalness, emission, up to `MaxDirectionalLightCount` analytic directional lights (the first casts cascaded shadow maps), and environment lighting;
- renders the radiance cubemap as the background and uses irradiance/radiance for IBL;
- records exclusively through `GraphicsCommandList`/`RenderGraph`;
- owns and unloads its five renderer-specific shaders (PreDepth, Shadow, ForwardPBR, ForwardSky, Composite).

Sky and Forward are separate logical passes registered as separate `RenderGraph`
passes; they share one Dynamic Rendering scope because `Compile()` detects that
both declare the identical `SceneColor`/`SceneDepth` attachment set and merges
them automatically (see `RenderGraph.md`), not because either the call site or
the graph hand-merges them.

The inactive raster-test path that previously lived inside `PathTracerSceneRenderer` has been removed. It is not the modern forward renderer.

## Current command architecture

The Phase 2C.4A command foundation is now implemented:

- `CommandPool` owns a Vulkan command pool for a graphics or compute queue.
- `CommandBuffer` owns persistent per-frame command buffers, fences, timestamps, and pipeline-statistics queries.
- `CommandBufferView` is the single non-owning view of one active recording, regardless of whether its owner is frame-persistent or transient.
- `CommandListBase`, `GraphicsCommandList`, `ComputeCommandList`, `TransferCommandList`, and `RayTracingCommandList` record exclusively through `CommandBufferView`.
- `TransientCommandBuffer` is a move-only lease returned by `TransientCommandExecutor`.
- `TransientCommandExecutor` reuses one synchronous fence and recycles native command buffers instead of allocating/freeing them for every operation.
- Until explicit queue-family ownership tracking exists, synchronous graphics and compute setup work uses one graphics+compute queue family.
- `ComputePipeline` is passive and owns pipeline state only; `ResourceManager` owns it in a stable-address generational pool.
- queued compute-pipeline initialization captures `Handle<ComputePipeline>`, never a raw `this` pointer, and validates the generation before creating native state.
- `DrawStreamBuilder` is retained; graphics execution belongs to `GraphicsCommandList`.
- uploads, copies, blits, and image transitions record through `TransferCommandList`; acceleration-structure operations record through `RayTracingCommandList`.
- all of those immediate workloads, plus ImGui setup, BRDF generation, and environment baking, use the centralized transient executor for submission.
- `ShaderBindingTable` owns its aligned ray-generation, miss, hit, and callable regions; callers do not rebuild SBT offsets during dispatch.
- `PathTracerSceneRenderer` records bind, image-barrier, and trace operations through `RayTracingCommandList` and the shared command base.
- `TransientCommandBuffer` has no implicit conversion to `VkCommandBuffer`; the Dear ImGui font-upload backend is the sole explicit native transient-handle boundary.

The swapchain already separates `m_CurrentCmdBufferIndex` (frame-in-flight slot) from `m_CurrentImageIndex` (acquired presentation image). A future `FrameContext` should aggregate the former with per-frame services; it must not collapse these two indices or recreate state that already exists.

## Target command architecture

The command system separates three concerns:

```text
Ownership/lifetime       Recording                         Submission
------------------       -----------------------------     -------------------------
CommandPool              CommandListBase                  frame lifecycle
CommandBuffer            GraphicsCommandList              persistent queue submit
Transient lease          ComputeCommandList               TransientCommandExecutor
CommandBufferView        TransferCommandList
                         RayTracingCommandList
```

### CommandPool

`CommandPool` remains the owner of `VkCommandPool`. It allocates and recycles native command buffers for one queue family.

Command pools are execution infrastructure, not `ResourceManager` assets.

### CommandBuffer

`CommandBuffer` owns persistent command-buffer storage and synchronization:

- one native command buffer per relevant frame slot;
- fences and query pools;
- begin/end/reset lifecycle;
- submission integration;
- GPU timestamp and pipeline-statistics result storage.

It does not implement high-level graphics or compute operations.

### CommandListBase

`CommandListBase` is a lightweight, non-owning typed view over a `CommandBufferView`. `CommandBufferView` is created only by `CommandBuffer` or `TransientCommandBuffer`, so command lists never accept a raw Vulkan command buffer directly.

Common functionality belongs here:

- generic pipeline barriers;
- debug labels;
- common push-constant plumbing;
- protected native-handle access for typed command-list implementations.

It does not allocate, end, submit, wait for, or destroy the command buffer.

If command-state caching is introduced, the cache must be shared by all typed views over the same recording context. Graphics and compute views must not maintain contradictory copies of native binding state.

### GraphicsCommandList

`GraphicsCommandList` records graphics-only operations:

- begin/end dynamic rendering or render passes;
- bind graphics pipeline;
- set viewport/scissor and dynamic state;
- bind vertex and index buffers;
- bind graphics descriptor sets/bind groups;
- draw and draw-indexed operations;
- fullscreen draws;
- `ExecuteDrawStream`.

Its primitive recording API is:

```text
BeginRendering / EndRendering
BindGraphicsPipeline
BindVertexBuffer / BindIndexBuffer
BindDescriptorSets
SetViewport / SetScissor
Draw / DrawIndexed
```

Higher-level helpers such as fullscreen rendering, draw-stream execution, and mesh-instance submission call these primitives instead of emitting duplicate Vulkan binding or draw calls.

It increments CPU-side draw statistics at the point where a draw command is actually emitted.

### ComputeCommandList

`ComputeCommandList` records compute-only operations:

- bind compute pipeline;
- bind compute descriptor sets/bind groups;
- set compute push constants;
- dispatch.

It does not own the pipeline, command buffer, queue, or fence. It records into either a frame command buffer or a transient command-buffer lease supplied by another owner.

### TransferCommandList

`TransferCommandList` records transfer-domain operations:

- buffer copies;
- buffer-to-image copies;
- image blits;
- inferred image-layout transitions;
- explicit image barriers for subresource-aware mip generation.

Texture uploads, staging-buffer copies, mip generation, and acceleration-structure instance uploads use this API. Image layout tracking remains owned by `Image`; the command list only records the Vulkan operation.

### RayTracingCommandList

`RayTracingCommandList` is the typed view for:

- bind ray-tracing pipeline;
- bind ray-tracing descriptors;
- push constants;
- trace rays using regions owned by `ShaderBindingTable`.
- build and compact acceleration structures;
- write acceleration-structure compaction queries.

It shares the same `CommandBufferView`, descriptor binding convention, statistics, and base barrier utilities as graphics and compute recording.

### TransientCommandExecutor

`TransientCommandExecutor` owns one-shot command allocation and submission policy. Before cross-queue ownership tracking exists, it uses the renderer's immediate graphics+compute pool and provides a move-only transient recording scope.

Its responsibilities are:

- acquire/reset a command buffer from the immediate pool;
- begin one-shot recording;
- expose an appropriate non-owning command-list view;
- end and submit the native command buffer;
- wait when the caller explicitly requests synchronous completion;
- recycle command buffers and fences.

It must avoid creating and destroying a Vulkan fence for every submission. The current implementation reuses one fence for the synchronous immediate context.

`vkQueueWaitIdle` is not part of the normal path. It remains an explicit exceptional operation for shutdown or API requirements.

Example target usage:

```cpp
auto transient = transientExecutor.BeginCompute();
ComputeCommandList compute(transient.GetRecordingView());

compute.BindPipeline(environmentFilterPipelineHandle);
compute.BindDescriptorSets(descriptorSets);
compute.Dispatch(groupCountX, groupCountY, groupCountZ);

transient.SubmitAndWait();
```

The transient scope owns the command-buffer lease. `ComputeCommandList` only borrows the common recording view. The executor continues to access the native handle internally for Vulkan allocation, begin/end, submission, and recycling; that backend access is ownership infrastructure, not duplicated recording API.

## RenderGraph

See [`RenderGraph.md`](RenderGraph.md) for a full walkthrough of the
implementation, its benefits, and what's still deferred. Summary:

`RenderGraph` (`Renderer/RenderGraph/RenderGraph.h`/`.cpp`) owns per-frame pass
ordering and barrier computation for both `ForwardSceneRenderer` and
`PathTracerSceneRenderer`: **pass ordering and precise, per-resource barriers
derived from declared reads/writes** (increment 1), plus **automatic Dynamic
Rendering scope merging, pass culling, and a debug/GPU-timing panel**
(increment 2). Transient/aliased resource allocation is an explicitly
separate, later increment — every resource the graph touches is still
imported from a `Handle<RenderTargetTexture>`/`Handle<Image>` the owning
`SceneRenderer` creates and destroys itself, exactly as before the graph
existed.

Researched against Frostbite's FrameGraph (GDC17), Unreal Engine 5's RDG, and
Granite's Vulkan render graph before designing this; all three converge on a
lambda-based `AddPass(name, setup, execute)` builder over a Setup -> Compile ->
Execute lifecycle, with barriers derived from declared access rather than
hand-written per call site. Cerberus keeps its four typed command-list domains
(`GraphicsCommandList`/`ComputeCommandList`/`TransferCommandList`/
`RayTracingCommandList`) instead of Unreal's one untyped `FRHICommandList`, so
`AddPass` is split per domain: `AddGraphicsPass`, `AddComputePass`,
`AddRayTracingPass`.

### API shape

```cpp
RGTextureHandle sceneColor = graph.ImportRenderTarget(m_SceneColor, "SceneColor");
RGTextureHandle sceneDepth = graph.ImportRenderTarget(m_SceneDepth, "SceneDepth");

graph.AddGraphicsPass(
    "ForwardOpaque",
    [=](RenderGraphBuilder& builder)
    {
        builder.WriteColorAttachment(sceneColor, LOAD_OP::CLEAR, STORE_OP::STORE);
        builder.WriteDepthAttachment(sceneDepth, LOAD_OP::LOAD, STORE_OP::STORE);
    },
    [=](GraphicsCommandList& graphics)
    {
        RenderSkyPass(graphics, ...);
        RenderForwardPass(graphics, ...);
    });

graph.Compile();
graph.Execute(*commandBuffer, frameIndex, &statistics);
```

`ImportRenderTarget`/`ImportImage` are called once per resource per frame, on
the graph itself, before any pass that reads/writes it — not inside a pass's
setup. A `RenderGraphBuilder` instance is scoped to one pass's setup call, and
the same underlying resource is typically touched by several passes (PreDepth
and Forward both write SceneDepth; Forward and Composite both touch
SceneColor), so importing it per-pass would hand out a distinct
`RGTextureHandle` to each call and break dependency tracking between them.

`RGTextureHandle` is a per-graph, per-frame handle distinct from
`Handle<RenderTargetTexture>`/`Handle<Image>`: it indexes the graph's own
access-history table, not `ResourceManager`'s generational pools.

This is a deliberate, explicit exception to the "no closures, no virtual
interfaces" convention the Dynamic Rendering migration otherwise follows — the
lambda-based builder was chosen over a free-function-only alternative because
it matches every major engine's actual graph API and keeps a pass's resource
declarations next to the code that uses them.

### Compile and Execute

`Compile()` builds a dependency graph from declared accesses (a pass depends on
every earlier pass that wrote a resource it reads or writes, and on every
earlier reader of a resource it writes), then topologically sorts it (Kahn's
algorithm, ties broken by registration order — so passes with no
cross-dependency, such as the shadow cascades, which each write a different
target, keep the order they were registered in). It then walks resources in
that compiled order to resolve each access's exact `Before`/`After`
synchronization scope from real per-frame history.

This replaces the conservative, always-identical barrier
`GraphicsCommandList::BeginRendering` used to hard-code for every call (an
access mask covering both "a previous frame's shader-read" and "another pass's
attachment write reused via `LOAD_OP::LOAD`" simultaneously, because it had no
visibility into which one actually happened). A resource's first touch within a
frame still uses that same conservative mask — the graph has no cross-frame
history for it either — but every later access within the same frame now gets
the exact producer access/stage instead of a guess. `Image`'s own
`m_CurrentImgLayout` tracking still resolves each barrier's old layout, exactly
as it does for code that is not on the graph.

`Execute()` records every pass's resolved barriers, then its execute callback,
in compiled order, into one `CommandBuffer` recording — still one
`BeginRenderThread`/`EndRenderThread`/`SubmitRenderThread` per frame at the call
site, no per-pass submission. A graphics pass with declared attachment writes
records through `GraphicsCommandList::BeginRenderingAttachments`/
`EndRenderingAttachments` — the same `vkCmdBeginRendering`/`vkCmdEndRendering`
pair as `BeginRendering`/`EndRendering`, but without inserting a barrier itself,
since `Execute()` already recorded the graph-resolved one immediately before.
Only `RenderGraph::Execute` calls these; any code not yet on the graph keeps
using the self-barriering `BeginRendering`/`EndRendering`.

A shader-resource render target nothing in the graph reads back (Forward's
`CompositeOutput`, sampled later by the editor viewport outside this graph) gets
one closing transition to a shader-read layout after the last pass, mirroring
what `EndRendering` used to do unconditionally for every shader-resource
attachment when its scope closed.

### Implemented (increment 2)

- Automatic Dynamic Rendering scope merging: consecutive graphics passes that
  declare an identical attachment set are folded into one scope by
  `Compile()`/`Execute()`. Sky and `ForwardGeometry` are registered as
  genuinely separate passes and recover their shared scope this way, instead
  of being hand-merged into one registered pass.
- Pass culling: a pass unreachable from anything that writes an imported
  resource is excluded before the topological sort. Culls nothing in
  Cerberus's current graph (every pass writes an imported resource today),
  same "correct but currently inert" shape barrier precision had at first
  touch — see `RenderGraph.md`.
- A RenderGraph debug/GPU-timing ImGui panel (`RenderGraph::OnImGuiRender`),
  in both scene renderers' existing "Renderer" window.

### Explicitly deferred

- Transient/aliased resource allocation — the graph taking over render-target
  creation/destruction and reusing memory across non-overlapping resource
  lifetimes. Investigated for increment 2 and deferred on its merits (no real
  aliasing candidates in Cerberus's current resource set — see
  `RenderGraph.md`'s "What's missing"), not on sequencing.
- Pass culling beyond simple reachability; there is no priority- or
  cost-based scheduling.
- Async compute / cross-queue scheduling — still gated on explicit queue-family
  ownership tracking, independent of the graph (see "Compute policy before
  RenderGraph" below).

## Compute policy before RenderGraph

Phase 2C.4 does not introduce true asynchronous compute.

- The device selects a graphics queue family that also supports compute.
- Frame-scoped and synchronous setup/baking compute records on that same queue family.
- A dedicated compute queue is discovered by `Device` but is not used for renderer resources yet.
- Queue-family ownership transfers, timeline-semaphore scheduling, and cross-queue pass dependencies are deferred.
- True async compute is reconsidered when RenderGraph resource dependencies can drive scheduling safely.

This prevents invalid cross-family resource access without introducing synchronization architecture twice.

## Pipeline ownership

Pipeline wrappers are passive GPU resources. Graphics, compute, and ray-tracing pipelines are created, resolved, and destroyed through `ResourceManager` handles.

`ComputePipeline` should own:

- compute shader reference/handle;
- `VkPipeline`;
- `VkPipelineLayout`;
- pipeline cache where retained;
- deferred native destruction.

It should not own:

- `m_ActiveComputeCommandBuffer`;
- a transient command buffer;
- graphics-vs-compute queue mode;
- `Begin`, `End`, or submit logic;
- global compute fences.

The same principle applies to graphics and ray-tracing pipeline wrappers.

## DrawStream and mesh submission

`DrawStreamBuilder` is retained. It is the compact CPU-side draw encoding used to reduce repeated state and prepare raster batching.

Responsibilities are divided as follows:

- `DrawStreamBuilder` encodes state changes and draw packets into 32-bit words.
- `DrawStreamReader` decodes the stream with bounds validation.
- Handle values wider than one word are encoded/decoded without truncation or aliasing violations.
- `GraphicsCommandList::ExecuteDrawStream` consumes the encoded stream and emits Vulkan draw commands.
- `RenderScene::BuildDrawStream` builds the stream once per frame; `ForwardSceneRenderer` replays the same stream for PreDepth, every shadow cascade, and the Forward geometry pass instead of re-encoding it per pass. There is no separate mesh-instance submission path left — `RenderMeshInstances`/`RasterDrawPushConstants` were removed once every raster pass moved onto the stream.
- Per-draw state is `{ WorldTransform, MaterialIndex }`, pushed once per draw via a push constant every DrawStream-consuming vertex shader (PreDepth, Shadow, ForwardPBR) declares identically, so `ExecuteDrawStream` can push it unconditionally without special-casing per pipeline. Normal matrices are derived in-shader from `WorldTransform` rather than precomputed CPU-side.
- Draw statistics are incremented only when `Draw`/`DrawIndexed` is emitted.

The builder contains no Vulkan calls and owns no GPU resources.

## Statistics policy

CPU renderer statistics are frame-scoped.

- Reset CPU counters once in `Renderer::BeginFrame`.
- Never reset draw statistics in `EndRendering`.
- Increment draw count in `GraphicsCommandList::Draw` and `DrawIndexed`.
- Increment compute dispatch count in `ComputeCommandList::Dispatch` when that counter is added.
- Increment ray-tracing dispatch count in `RayTracingCommandList::TraceRays`.
- Keep Vulkan timestamp and pipeline-statistics query ownership in persistent `CommandBuffer` infrastructure.

This makes statistics independent of how many passes are executed in a frame.

`RenderScene::GetTotalTriangleCount()` and `GetRasterMeshInstances().size()`
are a separate category: scene-geometry counts, not frame-execution counts.
They are not part of `RendererFrameStatistics` and are never reset per frame
— they are recomputed on demand from the current mesh-instance registration,
so they stay correct across any number of passes without needing a reset
point at all. Both scene renderers show them in their "Renderer" window's
"Statistics" section, alongside the existing FPS/VRAM readout.

## Instrumentation

`Core/Profiler.h`/`.cpp` is a small, engine-wide CPU+GPU instrumentation
facility with one runtime switch (`Profiler::IsEnabled`/`SetEnabled`,
surfaced as the "Enable Profiling" checkbox in the "Profiler" ImGui window,
`EditorLayer::OnImGuiRender`). It is intentionally a flat per-frame list with
visual nesting (`CPUProfileEntry::Depth`), not a hierarchical flamegraph or
timeline — there's no frame track yet, just current-frame scope durations.

- CPU: `CORE_PROFILE_SCOPE(name)`/`CORE_PROFILE_FUNCTION()` construct a
  `ProfileScope`, an RAII timer (built on `Core/Timer.h`) that records its
  duration into `Profiler`'s per-frame entry list on destruction.
  `CPUProfileEntry::Name` stores the raw `const char*` rather than an owned
  `std::string` -- every call site passes a string literal or `__FUNCTION__`
  (static storage duration), so there is nothing to copy or allocate.
  `Profiler::BeginFrame()` (called from `Renderer::BeginFrame`) clears the
  entry list at the start of each frame. Instrumented today: `Scene::OnUpdate`,
  `RenderScene::BuildDrawStream`, `RenderScene::PrepareRayTracing`,
  `ForwardSceneRenderer::Render`/`PathTracerSceneRenderer::Render`, and
  `Application::Run`'s top-level `ImGuiRender`/`LayerUpdate`/`FrameSubmit`/
  `Present` phases. Residual cost while the switch is off: `ProfileScope`
  still constructs its `Timer` member unconditionally (one clock read; a
  C++ member always constructs), and the constructor's `IsEnabled()` check
  happens after that -- not literally zero, but no allocation, no vector
  push, nothing beyond that one clock read.
- GPU: reuses `CommandBuffer`'s timestamp-query API (see "RenderGraph" above
  for its queued-vs-immediate distinction). Gated at two levels: `CommandBuffer::
  BeginRenderThread`/`EndRenderThread`/`SubmitRenderThread` pass
  `Profiler::IsEnabled()` as `enableQueries`/`collectQueryResults` (so the
  whole-frame timestamp pair, the pipeline-statistics query, the per-frame
  `vkCmdResetQueryPool`, and the `vkGetQueryPoolResults` readback are all
  skipped outright when off -- not just the per-pass ones), and
  `RenderGraph::Execute()` separately checks `Profiler::IsEnabled()` before
  each pass's `BeginTimestampQueryRenderThread`/`EndTimestampQueryRenderThread`
  pair. Every per-pass timestamp Cerberus records uses `TOP_OF_PIPE`/
  `BOTTOM_OF_PIPE` stages specifically because they don't wait for any actual
  GPU work to reach that point, avoiding the pipeline-stall risk a timestamp
  at a mid-pipeline stage (e.g. `FRAGMENT_SHADER`) would carry. The Profiler
  window shows "Frame Total (GPU)" from the whole-recording timestamp pair
  (query index 0/1) whenever profiling is on. None of these costs have been
  measured; the design choices above (cheapest possible timestamp stages,
  full skip rather than a hidden-but-still-running query when off) are meant
  to keep the plausible overhead low, not a substitute for actually profiling
  the profiler.
- `CommandBuffer::Invalidate`'s timestamp query pool budget (`maxUserQueries`
  in `CommandBuffer.cpp`) is 64, up from RenderGraph's original 16, with a
  `CORE_ASSERT` guard in both `BeginTimestampQuery` and
  `BeginTimestampQueryRenderThread` so exceeding it fails loudly in Debug
  instead of writing past the query pool.

## API input policy

Immediate recording APIs that consume contiguous caller-owned data use `std::span` instead of `std::vector` references.

Examples:

```cpp
void ExecuteDrawStream(std::span<const uint32_t> words);
void RenderMeshInstances(std::span<const Handle<MeshInstanceProxy>> instances,
                         std::span<const MaterialProxy> materials);
void BindDescriptorSets(std::span<const VkDescriptorSet> descriptorSets,
                        uint32_t firstSet = 0);
void PushConstants(std::span<const std::byte> data);
```

`std::span` communicates non-ownership and avoids forcing callers to allocate a particular container. The callee must not retain a span beyond the call unless the backing storage has an explicitly guaranteed lifetime.

## Phase 2C.4A: eliminate the legacy renderer

### Remove directly

- the `OLD_RENDERER` definition after all guarded blocks are resolved;
- duplicated old paths in `Renderer.cpp`, `Texture.cpp`, `UniformBuffer.cpp`, and `Material.cpp`;
- commented-out `RenderMesh` and `RenderMeshWithMaterial` implementations;
- dead global material-update and compute-dispatch facade APIs with no active callers;
- obsolete includes exposed only by deleted APIs;
- render-thread aliases such as `RT_Begin`, `RT_End`, and `RT_Submit` after all callers use the explicit API;
- implicit transient-command-buffer conversion to `VkCommandBuffer` after all engine callers use typed command lists;
- renderer-owned dead `#if 0` implementations that have a working replacement.

### Adapt or substitute

| Current responsibility | Target owner |
| --- | --- |
| legacy `Renderer::BeginRenderPass` / `EndRenderPass` | `GraphicsCommandList::BeginRendering` / `EndRendering` |
| fullscreen and indexed draw recording | `GraphicsCommandList` |
| `Renderer::RenderRenderPass` DrawStream execution | `GraphicsCommandList::ExecuteDrawStream` |
| `Renderer::RenderMeshInstances` Vulkan calls | `GraphicsCommandList` |
| `ComputePipeline::Begin/Dispatch/End` | passive `ComputePipeline` + `ComputeCommandList` |
| `Renderer::BeginTransientCommandBuffer` | `TransientCommandExecutor::Begin` |
| buffer/image copies, blits, and layout transitions | `TransferCommandList` |
| environment compute submission | `EnvironmentMapBaker` + transient compute list |
| path-tracer orchestration | `RayTracingCommandList`, then `PathTracePass` |

### Treat as explicit future work

- Disabled TLAS update/refit code is not a safe active implementation. Keep the stable rebuild path and record TLAS refit as a dedicated future optimization.
- The disabled legacy `ForwardSceneRenderer` was removed rather than adapted line by line. The active replacement shares no legacy material, pipeline, or submission code.
- The inactive raster test embedded in `PathTracerSceneRenderer` was removed; the new forward path was built independently.
- Third-party preprocessor guards are not modified by this phase.

## Migration sequence

### Implementation status — 2026-08-21

- 2C.4A.1 mechanical cleanup: implemented; only the third-party ImGuizmo preprocessor guard remains as a documented exception.
- 2C.4A.2 command recording split: implemented.
- 2C.4A.3 transient execution migration: implemented with recycled fences and command buffers.
- 2C.4A.4 renderer facade reduction: implemented in source; Windows build and validation-layer runtime verification remain required before the submit is considered fully validated.
- The disabled legacy `ForwardSceneRenderer` implementation was removed completely. A new implementation now exists against `RenderScene`, resource handles, persistent bind groups, and `GraphicsCommandList`.
- The unused standalone `Fence` wrapper and legacy `Shadow` helper were removed; persistent and transient synchronization remain owned by the command infrastructure.
- Synchronous compute now shares the graphics+compute immediate queue; the dedicated compute queue remains inactive until ownership transfers are modeled.
- Deferred command-buffer destruction captures the native `VkCommandPool`, never a `CommandPool*` wrapper.
- VMA allocation flags passed to `ResourceAllocator::AllocateBuffer` are no longer ignored.
- Renderer configuration now has one source of truth; `ResourceManager` exposes only the frame-count query it actually needs.
- Immediate command, descriptor-array, mesh-proxy, acceleration-structure, and swapchain-selection inputs use `std::span` where ownership is not transferred.
- `EnvironmentMapBaker` is implemented and owns all environment compute orchestration outside `Renderer.cpp`.
- compute-pipeline resources are registered in `ResourceManager`; BRDF and environment baking bind them through `ComputeCommandList` handles.
- `CommandBufferView` removes duplicated persistent/transient constructors from all typed command lists.
- `TransferCommandList` owns buffer/image copies, blits, and layout-transition recording; the obsolete `VulkanUtility` facade was removed.
- `RayTracingCommandList` owns RT pipeline/descriptor binding, tracing, AS build/compaction, and AS query recording, while `ShaderBindingTable` owns dispatch regions.
- `Image::Invalidate` resets tracked layout state when recreating its Vulkan image.
- `PathTracePass` and forward pass extraction remain later work.
- Renderer selection is persisted per scene, while `EditorLayer` keeps exclusive ownership and performs a full GPU-safe teardown before constructing the replacement.
- Renderer-retained bind groups use persistent descriptor allocation. Transient allocation remains limited to work rebuilt inside a frame or a synchronous transient operation.
- The path tracer unloads its SBT/pipeline-specific `Raytrace` shader state during renderer replacement; Forward does the same for `ForwardPBR` and `ForwardSky`.
- The legacy shader-backed `Material` mirror and `MeshSource::m_MeshShader` were removed. Global resources no longer load the unused `PBR`, `SceneComposite`, `Skybox`, `PreDepth`, or `ShadowMap` shaders.

### 2C.4A.1 - Mechanical legacy cleanup

- classify every renderer-owned `OLD_RENDERER`, `#if 0`, and commented implementation;
- delete branches already replaced by active code;
- remove dead facade APIs and includes;
- fix frame-statistics reset behavior;
- apply `std::span` to immediate non-owning inputs;
- preserve the stable path tracer and current environment loading.

### 2C.4A.2 - Command recording split

- introduce `CommandBufferView` and `CommandListBase`;
- introduce `GraphicsCommandList`, `ComputeCommandList`, `TransferCommandList`, and `RayTracingCommandList` as non-owning typed views;
- make `ComputePipeline` passive;
- preserve native-handle escape hatches for incremental migration;
- move DrawStream execution out of `Renderer`.

### 2C.4A.3 - Transient execution migration

- introduce `TransientCommandExecutor`;
- migrate texture upload, buffer upload, image transition, environment baking, acceleration-structure build, and ImGui setup callers;
- recycle fences and command buffers;
- remove the old transient facade only after the caller list reaches zero.

### 2C.4A.4 - Renderer facade reduction

- remove graphics and compute command-recording methods from `Renderer`;
- keep only lifecycle and service coordination;
- verify shutdown ordering and deferred deletion;
- verify no renderer-owned `OLD_RENDERER` or dead `#if 0` remains.

### 2C.4B - Rebuild raster rendering

- **Implemented:** new `ForwardSceneRenderer` against `RenderScene` and material proxies;
- **Implemented:** modern resource handles, persistent bind groups, HDR/depth output, environment/direct lighting, material texture sampling, and typed graphics recording;
- **Implemented:** exclusive editor switching with per-scene serialization and complete inactive-renderer teardown;
- **Implemented:** Windows/MSVC build, shader compilation, and Vulkan validation layers clean across Forward, PathTracer, and switching between them;
- **Implemented:** Dynamic Rendering migration (`0d09b4d`) — `GraphicsCommandList::BeginRendering`/`EndRendering` replace `VkRenderPass`/`VkFramebuffer` entirely;
- **Implemented:** explicit `PreDepth`, `Shadow` (cascaded shadow maps), `Sky`, `Forward`, and `Composite` passes, all consuming one `DrawStreamBuilder`-encoded stream per frame (see "Scene renderer and pass boundaries" and "DrawStream and mesh submission" above);
- **Implemented:** RenderGraph increment 1 — pass ordering and precise barrier derivation for both `ForwardSceneRenderer` and `PathTracerSceneRenderer` (see "RenderGraph" above);
- **Next:** shadow visual quality (bias/flicker tuning) is an open, parked issue, independent of pass/graph structure.

### Later phases

1. ~~Validate renderer switching, material parity, lighting, resource destruction, and descriptor lifetime on Windows/Vulkan.~~ Done.
2. ~~Extract `ForwardGeometryPass`, `SkyboxPass`, and `PathTracePass` without changing renderer ownership.~~ Done — PreDepth/Shadow/Sky/Forward/Composite are explicit passes for the forward renderer; the path tracer's per-frame work is one ray-tracing pass.
3. Build the deferred-hybrid renderer from explicit GBuffer, lighting, RT effect, and composite passes while keeping the full path tracer as a reference renderer.
4. ~~Introduce the RenderGraph skeleton only when multiple real passes can declare reads/writes and demonstrate barrier/transient-resource value.~~ Done — see "RenderGraph" above.
5. Add transient-resource aliasing and optional async-compute scheduling incrementally. (Dependency tracking and automatic barriers are done as of increment 1.)

## Phase 2C.4A definition of done

- No project-owned `OLD_RENDERER` macro or guarded legacy implementation remains.
- No renderer-owned dead `#if 0` implementation remains without a documented exception.
- `Renderer` no longer records raster draws or compute dispatches.
- `ComputePipeline` owns no command-buffer or submission state.
- All typed command lists borrow a `CommandBufferView`; they never own command buffers.
- `DrawStreamBuilder` remains functional and is consumed through the graphics command layer.
- Transient callers use `TransientCommandExecutor` or an explicitly documented migration bridge.
- Draw statistics reset once per frame and reflect every emitted draw across all passes.
- Immediate contiguous inputs use `std::span` where ownership is not transferred.
- Path tracing, environment loading, editor panels, uploads, and acceleration-structure builds remain operational.
- Validation layers report no new lifetime, synchronization, descriptor, or command-buffer errors.

## RenderGraph gate

The graph was gated on all of the following, which are now true:

- renderer ownership is clean;
- legacy renderer code is gone;
- command recording and submission are separated;
- material/mesh data is stable in `RenderScene`;
- path tracing and forward rendering use explicit passes;
- transient execution is centralized;
- statistics and frame indices are reliable.

Increment 1 (pass ordering, declared resource reads/writes, precise
per-resource barriers) and increment 2 (automatic scope merging, pass
culling, debug/timing panel) are implemented for both scene renderers — see
"RenderGraph" above and `RenderGraph.md`. Transient-resource allocation and
optional cross-queue scheduling remain future increments.

## Windows/Vulkan validation checklist for Phase 2C.4B

Before submitting the Forward baseline:

1. Clean and rebuild `Debug|x64` so both Forward shader pairs are compiled from source.
2. Start with Vulkan validation layers enabled and open the existing material/environment test scene.
3. Select `Renderer > Forward`; verify meshes, per-submesh materials, normal maps, environment background/IBL, exposure, and directional-light intensity.
4. Save, close, and reopen the scene; verify the selected renderer is restored from the `Renderer` scene field.
5. Switch Forward -> Path Tracer -> Forward at least twenty times. Verify no `VK_ERROR_OUT_OF_POOL_MEMORY`, null descriptor, use-after-free, or increasing inactive-renderer VRAM trend.
6. In Path Tracer, verify accumulation, TLAS instances, duplicate mesh actors, material edits, and light/environment edits still work after a switch.
7. Resize and maximize/restore the viewport in both renderers; verify the displayed image view remains valid.

Expected limitation of this baseline: Forward uses an approximate
split-sum-free specular IBL term. That is a renderer feature, not a reason to
retain any legacy implementation. Cascaded shadow maps are implemented; visual
quality (bias/flicker under some view angles) is a separate, tracked follow-up.
