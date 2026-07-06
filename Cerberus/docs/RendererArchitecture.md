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
- `MaterialProxy::GPUData` is the source for the future GPU material storage buffer and ray-tracing material lookup. Bindless texture indices are still invalid until the bindless texture-table phase.
