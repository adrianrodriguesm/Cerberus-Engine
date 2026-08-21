# Cerberus Renderer Architecture

Last updated: 2026-08-21

This document defines the current renderer ownership model, the target command-recording architecture, and the migration plan that must be completed before introducing a RenderGraph.

The immediate priority is stability: the path tracer, editor, scene data, materials, environment lighting, and GPU object lifetime must remain reliable while the remaining legacy renderer is removed.

## Architectural constraints

1. `Renderer` remains a small global facade and frame-lifecycle coordinator.
2. Scene data, render passes, resource ownership, command recording, and command submission must have separate owners.
3. No two wrappers may believe they own the same `VkCommandBuffer` or native resource.
4. `GraphicsCommandList`, `ComputeCommandList`, and `RayTracingCommandList` are typed, non-owning recording views. They do not allocate, submit, or destroy command buffers.
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

- white, black, and default-normal textures;
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

`Renderer` only coordinates its lifetime.

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

### Material ownership and render proxies

- `MaterialAsset` owns serializable CPU values and texture asset handles.
- `MeshSource` owns imported material slots; `Submesh::MaterialIndex` indexes those slots.
- A mesh instance's `MaterialTable` may override any source material slot.
- `RenderScene` resolves the effective material per registered submesh and creates a scene-local `MaterialProxy`.
- `MeshInstanceProxy::MaterialProxyIndex` is a render-side index, not an asset pointer or raster bind group.
- `MaterialProxy::GPUData` is uploaded to the scene-local material buffer.
- Multiple actors may reference the same `MeshSource`; actor identity and transform belong to separate mesh-instance proxies.
- The TLAS custom instance index maps a hit to `RayTracingInstanceData`, which maps to the correct mesh instance, geometry, and GPU material index.
- Closest-hit reconstructs surface data; path integration and BSDF sampling remain in raygen.
- Material texture indices address the scene-local RT texture table, which is populated with valid fallback descriptors.

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

`PathTracerSceneRenderer` currently performs too much inline work. Its target responsibility is:

- own the path-tracing view/output state;
- update path-tracing settings and accumulation state;
- request acceleration-structure preparation from `RenderScene`;
- invoke `PathTracePass` and later composition/post-processing passes.

The same pass separation applies to the forward renderer. The old disabled `ForwardSceneRenderer` must not be ported line by line. It will be rebuilt around the new data model with passes such as:

- `ForwardGeometryPass`;
- `SkyboxPass`;
- optional depth/shadow passes;
- `CompositePass` where required.

The inactive raster-test path that previously lived inside `PathTracerSceneRenderer` has been removed. It is not the future forward renderer.

## Current command architecture

The Phase 2C.4A command foundation is now implemented:

- `CommandPool` owns a Vulkan command pool for a graphics or compute queue.
- `CommandBuffer` owns persistent per-frame command buffers, fences, timestamps, and pipeline-statistics queries.
- `CommandBufferView` is the single non-owning view of one active recording, regardless of whether its owner is frame-persistent or transient.
- `CommandListBase`, `GraphicsCommandList`, `ComputeCommandList`, and `RayTracingCommandList` record exclusively through `CommandBufferView`.
- `TransientCommandBuffer` is a move-only lease returned by `TransientCommandExecutor`.
- `TransientCommandExecutor` reuses one synchronous fence and recycles native command buffers instead of allocating/freeing them for every operation.
- Until explicit queue-family ownership tracking exists, synchronous graphics and compute setup work uses one graphics+compute queue family.
- `ComputePipeline` is passive and owns pipeline state only; `ResourceManager` owns it in a stable-address generational pool.
- queued compute-pipeline initialization captures `Handle<ComputePipeline>`, never a raw `this` pointer, and validates the generation before creating native state.
- `DrawStreamBuilder` is retained; graphics execution belongs to `GraphicsCommandList`.
- uploads, image transitions, acceleration-structure builds, ImGui setup, BRDF generation, and environment baking use the centralized transient executor.
- `ShaderBindingTable` owns its aligned ray-generation, miss, hit, and callable regions; callers do not rebuild SBT offsets during dispatch.
- `PathTracerSceneRenderer` records bind and trace operations through `RayTracingCommandList`. Its remaining native image-barrier call is an explicit bridge until image transitions are expressed through the shared command layer.

The swapchain already separates `m_CurrentCmdBufferIndex` (frame-in-flight slot) from `m_CurrentImageIndex` (acquired presentation image). A future `FrameContext` should aggregate the former with per-frame services; it must not collapse these two indices or recreate state that already exists.

## Target command architecture

The command system separates three concerns:

```text
Ownership/lifetime       Recording                         Submission
------------------       -----------------------------     -------------------------
CommandPool              CommandListBase                  frame lifecycle
CommandBuffer            GraphicsCommandList              persistent queue submit
Transient lease          ComputeCommandList               TransientCommandExecutor
CommandBufferView        RayTracingCommandList
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

- barriers and resource transitions;
- buffer/image copies;
- debug labels;
- common push-constant plumbing;
- native-handle access as an explicit backend escape hatch.

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

### RayTracingCommandList

`RayTracingCommandList` is the typed view for:

- bind ray-tracing pipeline;
- bind ray-tracing descriptors;
- push constants;
- trace rays using regions owned by `ShaderBindingTable`.

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
- `RenderMeshInstances` remains a higher-level mesh-instance submission path during migration, but ultimately funnels into `GraphicsCommandList` rather than issuing Vulkan calls through `Renderer`.
- Material bind-group changes are represented explicitly in the stream.
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

## API input policy

Immediate recording APIs that consume contiguous caller-owned data use `std::span` instead of `std::vector` references.

Examples:

```cpp
void ExecuteDrawStream(std::span<const uint32_t> words);
void RenderMeshInstances(std::span<const Handle<MeshInstanceProxy>> instances);
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
- `TransientCommandBuffer::Release` if the final caller audit confirms that no raw ownership transfer remains;
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
| environment compute submission | `EnvironmentMapBaker` + transient compute list |
| path-tracer orchestration | `RayTracingCommandList`, then `PathTracePass` |

### Treat as explicit future work

- Disabled TLAS update/refit code is not a safe active implementation. Keep the stable rebuild path and record TLAS refit as a dedicated future optimization.
- The disabled legacy `ForwardSceneRenderer` is removed rather than adapted line by line. A new forward renderer is rebuilt after the command-list layer is stable.
- The inactive raster test embedded in `PathTracerSceneRenderer` has been removed; the new forward path will be built independently.
- Third-party preprocessor guards are not modified by this phase.

## Migration sequence

### Implementation status — 2026-08-21

- 2C.4A.1 mechanical cleanup: implemented; only the third-party ImGuizmo preprocessor guard remains as a documented exception.
- 2C.4A.2 command recording split: implemented.
- 2C.4A.3 transient execution migration: implemented with recycled fences and command buffers.
- 2C.4A.4 renderer facade reduction: implemented in source; Windows build and validation-layer runtime verification remain required before the submit is considered fully validated.
- The disabled legacy `ForwardSceneRenderer` implementation, declarations, project entries, and `Scene` friendship were removed completely. The future forward renderer will be introduced as a new API after the command-list layer is validated.
- The unused standalone `Fence` wrapper and legacy `Shadow` helper were removed; persistent and transient synchronization remain owned by the command infrastructure.
- Synchronous compute now shares the graphics+compute immediate queue; the dedicated compute queue remains inactive until ownership transfers are modeled.
- Deferred command-buffer destruction captures the native `VkCommandPool`, never a `CommandPool*` wrapper.
- VMA allocation flags passed to `ResourceAllocator::AllocateBuffer` are no longer ignored.
- Renderer configuration now has one source of truth; `ResourceManager` exposes only the frame-count query it actually needs.
- Immediate command, descriptor-array, mesh-proxy, acceleration-structure, and swapchain-selection inputs use `std::span` where ownership is not transferred.
- `EnvironmentMapBaker` is implemented and owns all environment compute orchestration outside `Renderer.cpp`.
- compute-pipeline resources are registered in `ResourceManager`; BRDF and environment baking bind them through `ComputeCommandList` handles.
- `CommandBufferView` removes duplicated persistent/transient constructors from all typed command lists.
- `RayTracingCommandList` owns RT pipeline/descriptor binding and trace recording, while `ShaderBindingTable` owns dispatch regions.
- `PathTracePass` and the new forward renderer remain later work.

### 2C.4A.1 - Mechanical legacy cleanup

- classify every renderer-owned `OLD_RENDERER`, `#if 0`, and commented implementation;
- delete branches already replaced by active code;
- remove dead facade APIs and includes;
- fix frame-statistics reset behavior;
- apply `std::span` to immediate non-owning inputs;
- preserve the stable path tracer and current environment loading.

### 2C.4A.2 - Command recording split

- introduce `CommandBufferView` and `CommandListBase`;
- introduce `GraphicsCommandList`, `ComputeCommandList`, and `RayTracingCommandList` as non-owning typed views;
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

- implement a new `ForwardSceneRenderer` against `RenderScene` and material proxies;
- use typed command lists and explicit passes;
- do not restore legacy `Material`, `Pipeline`, or global descriptor paths;
- validate DrawStream and direct mesh-instance submission against the same graphics recording layer.

### Later phases

1. Extract `PathTracePass` from `PathTracerSceneRenderer`.
2. Separate forward-renderer passes.
3. Introduce the RenderGraph skeleton only after ownership and pass boundaries are stable.
4. Add resource dependency tracking, barriers, transient resources, and optional async-compute scheduling incrementally.

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

RenderGraph remains frozen until all of the following are true:

- renderer ownership is clean;
- legacy renderer code is gone;
- command recording and submission are separated;
- material/mesh data is stable in `RenderScene`;
- path tracing and forward rendering use explicit passes;
- transient execution is centralized;
- statistics and frame indices are reliable.

Only then should the graph add pass ordering, declared resource reads/writes, barriers, transient-resource allocation, and optional cross-queue scheduling.
