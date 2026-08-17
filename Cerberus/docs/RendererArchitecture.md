# Renderer Responsibility Boundaries

This document captures the intended ownership model for the renderer after the lifetime cleanup.

## Renderer

`Renderer` is the public facade for the global rendering backend. It owns process-wide renderer state and coordinates frame begin/end, shutdown, command allocation helpers, and access to global systems such as `ResourceManager`, `RendererResources`, `ImGuiRenderer`, `RenderQueueManager`, and `DeferredResourceDeletionQueue`.

It should not own per-scene render data such as mesh registrations, BLAS/TLAS registries, camera state, or pass-specific resources.

## RendererResources

`RendererResources` owns renderer-wide default resources such as default textures, the default PBR material, the empty environment, and the global `ShaderLibrary`. `Renderer` coordinates its lifetime, but it should not store these concrete default resources directly in `RendererData`.

## ImGuiRenderer

`ImGuiRenderer` owns Dear ImGui backend state: the ImGui descriptor pool, secondary command buffers, backend init/shutdown, and swapchain ImGui rendering. `Renderer` coordinates its lifetime, but `RendererData` should not store ImGui-specific Vulkan objects directly.

## Renderer command state / CommandPool / CommandBuffer

`Renderer` owns renderer-level command execution infrastructure. That includes renderer-owned `CommandPool` wrappers and long-lived main/swapchain `CommandBuffer` wrappers. Raw `VkCommandPool` handles should stay hidden inside `CommandPool`; other systems should request persistent command buffers or transient command buffers through renderer-facing APIs instead of allocating from Vulkan pools directly.

`CommandPool` wraps a native `VkCommandPool` and centralizes command buffer allocation/free for a specific renderer queue family. It is not a `ResourceManager` resource because it belongs to frame execution infrastructure rather than asset/GPU data ownership.

`CommandBuffer` is the persistent frame command buffer wrapper. It allocates from a renderer-owned `CommandPool`, provides queued calls for callers outside the render thread, and explicit render-thread calls for code already executing on the render thread.

`TransientCommandBuffer` is the one-shot command buffer wrapper for uploads, copies, image transitions and setup work. It borrows a renderer-owned `CommandPool`, submits/waits, and returns the native command buffer to the pool.

## RendererContext / Device

`RendererContext` and `Device` own Vulkan instance/device level state: `VkDevice`, queues, physical device capabilities, and backend extension access.

## ResourceManager

`ResourceManager` owns engine wrappers and handle pools. It is responsible for creating and destroying engine-level resource wrappers. When a wrapper owns native Vulkan/VMA resources, destruction should extract native handles and submit only those native handles to `DeferredResourceDeletionQueue`.

It should not own descriptor allocation state. BindGroup and BindGroupLayout wrappers live in `ResourceManager`, but native descriptor pools and descriptor layout caching belong to `DescriptorManager`.

## DescriptorManager

`DescriptorManager` is the single owner of descriptor allocation and descriptor layout cache state. It owns transient descriptor pools, persistent descriptor pools, and the descriptor set layout cache. `Renderer` initializes and shuts it down; `ResourceManager`, `BindGroup`, and shader reflection request descriptor allocation/layouts through it but do not own its allocators.

## DeferredResourceDeletionQueue

`DeferredResourceDeletionQueue` owns delayed native GPU destruction. It should only capture native Vulkan/VMA objects by value. It should not capture engine wrappers, `ResourceManager` handles, `this`, or references.

## RenderQueueManager

`RenderQueueManager` owns CPU-side render command execution only. It should not own resource lifetime or delayed destruction.

## Scene

`Scene` is the logical/editor/game representation: entities, components, transforms, assets, and gameplay/editor data. It should not own Vulkan objects or acceleration structures.

## RenderScene

`RenderScene` is the renderer-side representation of a `Scene`: mesh proxies, material proxies, dirty flags, renderable instance state, and ray tracing acceleration structure data. It is the bridge between logical scene data and render passes.

## SceneRenderer / PathTracerSceneRenderer

`SceneRenderer` orchestrates rendering for a scene. Concrete renderers such as `PathTracerSceneRenderer` should consume `RenderScene`, own pass-local resources, and dispatch passes. They should not own global backend lifetime.

## Next direction

Before introducing a RenderGraph, keep the current goal focused on making ownership boundaries explicit:

1. Renderer state grouped by responsibility.
2. RendererResources owns default renderer resources and shader library.
3. RenderScene owns renderable scene data.
4. ResourceManager owns wrappers.
5. DescriptorManager owns descriptor pools and descriptor set layout cache.
6. DeferredResourceDeletionQueue owns native delayed destruction.
7. ImGuiRenderer owns Dear ImGui backend resources.
8. CommandPool wraps renderer command pools; CommandBuffer and TransientCommandBuffer allocate through it.
9. RenderQueueManager owns render command execution only.


## Material ownership and render proxies

- `MaterialAsset` owns serializable CPU-side values and texture **asset handles**.
- `MeshSource` owns imported material slots; `Submesh::MaterialIndex` indexes those slots.
- A `Mesh`/scene `MaterialTable` may override a source material slot per mesh instance.
- `RenderScene` resolves the effective material for every registered submesh and creates a scene-local `MaterialProxy` entry.
- `MeshInstanceProxy::MaterialProxyIndex` is the stable render-side material mapping. It is intentionally an index, not an asset pointer or raster bind group.
- `MaterialProxy::GPUData` is uploaded to the scene-local GPU material storage buffer.
- Cerberus currently builds one TLAS instance per submesh. `gl_InstanceCustomIndexEXT` indexes `RayTracingInstanceData`, which maps that hit back to the submesh's `MaterialProxy::GPUMaterialIndex`.
- `RayTracingInstanceData` also carries vertex/index buffer device addresses and submesh offsets. Closest-hit combines those with `gl_PrimitiveID` and barycentrics to reconstruct world position, geometric/shading normals and UVs.
- Closest-hit returns surface data and material parameters through `HitPayload`; path integration and BSDF sampling belong to raygen.
- `RenderScene` resolves material texture asset handles into scene-local GPU `Texture2D` resources. `GPUMaterialData::TextureIndices` indexes the fixed RT texture table at set 0/binding 5.
- The RT texture table is fully populated with valid fallback descriptors, so this first bindless phase only needs non-uniform sampled-image indexing; partially-bound and update-after-bind descriptors are not required yet.
- Closest-hit samples albedo, roughness, metalness and tangent-space normals with `textureLod(..., 0)`. Ray-cone/texture-footprint mip selection remains a later path-tracing phase.
- Assimp material import currently resolves external diffuse and normal-map files. Embedded source-file textures remain unsupported.

## Texture2D ownership

- `TextureAsset` is the CPU/file asset: decoded pixels plus import metadata. It does not own Vulkan resources.
- `Texture2D` is the GPU resource owned by `ResourceManager`. The asset-backed creation path accepts an explicit `TextureColorSpace`, so one source asset can be resident as sRGB color data or linear material data without mutating the asset.
- `RenderScene` no longer rebuilds texture properties manually; it resolves the asset and asks `ResourceManager` to create the GPU texture.
- Texture2D image views, samplers, ImGui access and the generated BRDF LUT now use handles and the current resource/bind-group path. Their old `OLD_RENDERER` branches have been removed.
- `TextureCube` and environment-map migration are intentionally the next phase; the remaining `OLD_RENDERER` texture blocks belong to that path.
