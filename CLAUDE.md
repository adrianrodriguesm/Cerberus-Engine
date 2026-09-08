# Cerberus Engine Development Guidelines

These guidelines define how Claude Code should work inside the Cerberus engine repository. Treat them as the default engineering contract unless the user explicitly overrides them for a specific task.

## 1. Core Priorities

When making changes, prioritize in this order:

1. **Correctness and stability** — the engine must keep compiling and rendering correctly.
2. **Clear ownership and responsibilities** — every resource and system should have an obvious owner.
3. **Minimal, focused changes** — do not combine unrelated refactors, feature work, dependency upgrades, and architecture changes in one task.
4. **Thin RHI, explicit renderer architecture** — renderer code should express rendering intent without leaking Vulkan details upward.
5. **Performance awareness** — avoid abstractions that introduce unnecessary allocations, synchronization, indirection, or duplicate GPU work.
6. **Future compatibility** — prefer solutions that move naturally toward the planned hybrid deferred renderer and RenderGraph without prematurely implementing them.

Do not optimize for the shortest amount of code if it makes ownership, execution, or resource lifetime less obvious.

## 2. Working Style

Before editing:

- Inspect the relevant implementation and its direct callers/users.
- Search for duplicate or legacy implementations before adding a new one.
- Identify ownership, lifetime, and destruction paths for any resource being changed.
- Understand the current command-buffer and synchronization path before modifying GPU execution.
- If the task may affect Vulkan feature support, inspect the actual SDK/header/VMA versions and device feature chain rather than assuming support from the requested API version.

During implementation:

- Make incremental changes that can be compiled and validated independently.
- Preserve current behavior unless the task explicitly requests a behavior change.
- Prefer a small architectural step over a large speculative framework.
- Reuse existing Cerberus types and conventions instead of creating parallel abstractions.
- Do not silently rewrite adjacent systems just because they could be cleaner.

If an unexpected architectural conflict appears, stop and explain the conflict before deleting or replacing real uncommitted work.

## 3. Cerberus Architecture Principles

Use object-oriented design where an entity has meaningful **identity, state, ownership, or lifetime**. Prefer data + functions for short-lived rendering operations.

Good persistent objects include:

- `Renderer`
- `SceneRenderer`
- `ForwardSceneRenderer`
- `PathTracerSceneRenderer`
- `RenderScene`
- `RenderGraph` (future)
- `CommandBuffer`
- `GraphicsCommandList`
- `GraphicsPipeline`
- GPU resources and allocators

Logical rendering passes should generally **not** become persistent polymorphic objects merely because they are called passes.

Prefer:

```cpp
struct ForwardPassParameters
{
    // Per-pass/per-frame data.
};

void RenderForwardPass(
    GraphicsCommandList& commandList,
    const ForwardPassParameters& parameters);
```

over:

```cpp
class IRenderPass
{
public:
    virtual void Initialize() = 0;
    virtual void Execute() = 0;
    virtual void Shutdown() = 0;
};
```

Do not introduce `IRenderPass`, `RenderPassBase`, or similar inheritance unless there is a concrete runtime requirement that justifies polymorphism.

## 4. Scene Renderer Responsibilities

A `SceneRenderer` is the high-level orchestrator for one rendering strategy.

For the Forward renderer, the intended direction is conceptually:

```text
ForwardSceneRenderer
    Prepare frame data
    PreDepth
    Shadow
    Sky
    Forward geometry
    Composite
```

The renderer owns or coordinates persistent renderer/frame resources such as:

- camera buffers
- lighting buffers
- material buffers
- scene color
- scene depth
- shadow resources where appropriate
- persistent PSOs/bindings needed by that renderer

A logical pass should receive only what it needs to perform its work.

The Path Tracer is a separate `SceneRenderer`. Switching renderer should fully tear down the inactive renderer so the engine does not pay the resource/runtime cost of both implementations simultaneously.

## 5. Logical Passes vs RHI Rendering Scopes

This distinction is mandatory:

> **Logical renderer pass != Vulkan render pass / RHI rendering scope.**

For example, `SkyPass` and `ForwardPass` are separate logical passes, but they may share one rendering scope:

```cpp
graphics.BeginRendering(renderingInfo);

RenderSkyPass(graphics, skyParameters);
RenderForwardPass(graphics, forwardParameters);

graphics.EndRendering();
```

Do not force every logical pass to call its own `BeginRendering()` / `EndRendering()`.

The current `SceneRenderer` may manually decide rendering scopes. In the future, the `RenderGraph` should take over much of this responsibility.

## 6. RenderGraph Direction

`RenderGraph` should be a real object because it owns meaningful frame-local state and lifetime information.

Its eventual responsibilities include:

- pass nodes
- resource nodes
- dependency tracking
- execution ordering
- resource state tracking
- barrier generation
- transient resource lifetime
- transient allocation/aliasing
- pass culling
- potentially rendering-scope construction and async scheduling

The desired future usage is conceptually:

```cpp
RenderGraph graph;

AddPreDepthPass(graph, ...);
AddShadowPass(graph, ...);
AddSkyPass(graph, ...);
AddForwardPass(graph, ...);
AddCompositePass(graph, ...);

graph.Compile();
graph.Execute(commandList);
```

Passes should describe work and resource usage; the graph should own scheduling and dependency machinery.

Do **not** implement RenderGraph functionality prematurely inside current pass helpers.

## 7. RHI Rules

The RHI should remain thin and explicit.

Renderer-level code should not directly use raw Vulkan types when a Cerberus RHI abstraction is appropriate.

Preferred direction:

```text
SceneRenderer / RenderGraph
        -> GraphicsCommandList
        -> Cerberus RHI objects/descriptions
        -> Vulkan
```

Avoid leaking:

```text
VkRenderingInfo
VkPipelineBarrier2
VkDescriptorSet
VkCommandBuffer
```

into high-level renderer code unless the code is specifically part of the Vulkan/RHI layer.

RHI abstractions should represent useful engine concepts, not merely rename a single Vulkan call without adding a meaningful boundary.

## 8. Dynamic Rendering Direction

Cerberus is moving from legacy Vulkan `VkRenderPass` / `VkFramebuffer` usage toward Vulkan Dynamic Rendering.

The intended modern raster path is:

```text
Cerberus RenderingInfo
        -> GraphicsCommandList::BeginRendering()
        -> VkRenderingInfo / VkRenderingAttachmentInfo
        -> vkCmdBeginRendering()
```

Graphics pipelines remain `VkPipeline` based. Dynamic Rendering does **not** imply migrating to `VK_EXT_shader_object`.

When Dynamic Rendering is implemented:

- remove the graphics-pipeline dependency on `VkRenderPass`;
- describe compatible color/depth/stencil formats in the graphics pipeline specification;
- use `VkPipelineRenderingCreateInfo` internally;
- move render-target ownership out of the old framebuffer abstraction;
- replace legacy implicit render-pass layout transitions with correct explicit synchronization;
- do not implement RenderGraph in the same change;
- do not upgrade Vulkan SDK/VMA unless inspection proves it is required.

## 9. Command Buffer and Submission Rules

Normal frame rendering should use the main graphics command buffer and avoid unnecessary submits.

Preferred structure:

```text
Begin main graphics command buffer
    PreDepth work
    Shadow work
    Forward/Sky work
    Composite work
End command buffer
Submit once
```

Do not submit once per logical pass unless there is a proven synchronization/queue requirement.

`TransientCommandExecutor` / transient command infrastructure is for one-shot or setup work such as uploads, resource initialization, or similar short-lived operations. It should not become the normal frame-rendering path.

Keep `CommandBuffer` lifetime/submission responsibilities separate from lightweight command-list views used to record graphics/compute/transfer/ray-tracing work.

## 10. Resource Ownership

Ownership must be explicit.

Ask these questions for every GPU resource:

- Who creates it?
- Who owns it?
- Who is allowed to reference it?
- Who resizes/recreates it?
- Who destroys it?
- Can more than one pass consume it?

Do not use a framebuffer or pass abstraction as the owner of textures merely because Vulkan historically grouped them together.

For example, the intended direction is:

```text
ForwardSceneRenderer
    owns SceneColor
    owns SceneDepth

PreDepth
    writes SceneDepth

Forward
    consumes the same SceneDepth
```

not:

```text
PreDepth framebuffer owns DepthA
Forward framebuffer owns DepthB
```

Prefer existing `Handle<>`, `Ref<>`, `ResourceManager`, allocator, and deferred-deletion conventions. Avoid introducing raw ownership beside systems already managed through handles.

## 11. ResourceManager

`ResourceManager` is allowed to create/cache/manage multiple categories of RHI resources. This does **not** mean all those types belong in a generic `Resources/` folder.

Keep taxonomy based on responsibility, for example:

```text
Renderer/RHI/Command/
Renderer/RHI/Pipeline/
Renderer/RHI/Buffers/
Renderer/Descriptor/
Renderer/Memory/
Renderer/RayTracing/
```

When an API becomes obsolete, remove it after all real users have migrated. Do not keep dead compatibility APIs "just in case" unless there is an explicit compatibility requirement.

## 12. Assets, RenderScene, and GPU Representation

Keep these layers distinct:

```text
Asset
    persistent/serializable content representation

RenderScene / Proxies
    renderer-facing representation of scene content

RHI resources
    concrete GPU objects and memory
```

Examples:

```text
MeshAsset != MeshProxy != GPUBuffer
MaterialAsset != MaterialProxy != descriptor/pipeline state
```

Do not merge these concepts simply because they refer to the same source content.

Renderer proxies belong to the rendering scene layer, not the asset layer and not a generic RHI resource folder.

## 13. Draw Submission

`DrawStreamBuilder` is intended to move mesh submission toward prepared, data-oriented draw streams.

Do not force a `DrawStreamBuilder` migration into unrelated pass/refactor work.

The intended long-term direction is conceptually:

```text
RenderScene / MeshInstanceProxy
        -> pass filtering/preparation
        -> DrawStreamBuilder
        -> DrawStream
        -> GraphicsCommandList
```

This is analogous in spirit to separating scene representation, pass-specific draw preparation, and final command recording.

Keep changes to mesh submission isolated and measurable.

## 14. Folders and Includes

Folders should reflect ownership/responsibility, not become generic dumping grounds.

Use full include paths from the `src` include root:

```cpp
#include "Renderer/RHI/Pipeline/Pipeline.h"
#include "Renderer/RHI/Command/GraphicsCommandList.h"
#include "Renderer/Scene/Proxies/MeshProxy.h"
#include "Asset/Assets/MeshAsset.h"
```

Avoid deep relative includes such as:

```cpp
#include "../../../Pipeline.h"
```

Do not add every subdirectory to Premake include paths to make short includes compile. `src` is the canonical project include root.

When moving files:

1. update all includes;
2. search for duplicate copies;
3. verify Premake globbing does not compile both old and new files;
4. remove the old copy only after confirming the new location is authoritative.

## 15. Naming and C++ Style

Follow the style already established by surrounding Cerberus code.

General rules:

- C++20.
- Prefer `final` for concrete classes that are not intended for inheritance.
- Prefer `const` correctness.
- Prefer references for required non-owning inputs; pointers only when nullability is meaningful.
- Use `Handle<>` / `Ref<>` consistently with current ownership systems.
- Avoid raw `new` / `delete` when the surrounding system already manages lifetime.
- Avoid introducing a second naming convention for existing concepts.
- Avoid clever template/meta-programming unless it clearly reduces complexity.
- Avoid inheritance when composition, plain data, or free functions express the relationship better.
- Keep functions focused; do not create trivial wrappers with no semantic value merely to increase abstraction layers.

## 16. Performance Expectations

Cerberus is a rendering engine project. Performance implications matter even in architecture work.

Before introducing a new abstraction, consider:

- allocation frequency;
- per-frame heap work;
- descriptor churn;
- pipeline creation/caching;
- synchronization and queue stalls;
- duplicate resource ownership;
- repeated CPU scene traversal;
- command-buffer submits;
- unnecessary virtual dispatch in hot paths;
- resource lifetime and transient memory opportunities.

Do not claim a performance improvement without measurement.

Architectural cleanup may be justified without measurable GPU gains, but say clearly when a change is structural rather than a performance optimization.

## 17. Change / CL Discipline

Keep CLs narrow and reviewable.

Good examples:

```text
CL: Extract logical Sky/Forward pass functions without changing output.
CL: Migrate raster path from VkRenderPass/VkFramebuffer to Dynamic Rendering.
CL: Introduce shared SceneDepth and add PreDepth.
CL: Move forward mesh submission to DrawStreamBuilder.
CL: Add explicit resource-state abstraction.
CL: Introduce RenderGraph.
```

Bad example:

```text
Update Vulkan SDK + update VMA + add Dynamic Rendering + redesign ResourceManager
+ add PreDepth + replace DrawStream + implement RenderGraph.
```

For structural refactors, aim for identical behavior/output before introducing the next feature.

## 18. Validation Requirements

After meaningful renderer/RHI changes:

- compile the affected target;
- compile Debug;
- compile Release when feasible;
- run the Forward renderer;
- run the Path Tracer when shared renderer infrastructure changed;
- verify Forward <-> PathTracer switching;
- verify resize/recreation paths;
- verify `GetFinalPassImage()` or equivalent output path;
- inspect Vulkan validation output;
- fix new synchronization/layout/lifetime validation errors;
- check destruction and deferred deletion for leaks/double-destruction;
- search for stale legacy symbols/call sites after migrations.

A renderer change is not complete merely because it compiles.

For rendering refactors, preserve visual output unless the task intentionally changes rendering behavior.

## 19. Avoid These Patterns

Do not introduce these without a concrete requirement:

- `IRenderPass` / `RenderPassBase` inheritance hierarchy;
- one persistent object per simple logical pass;
- one GPU submit per pass;
- renderer code directly calling Vulkan;
- a generic `Renderer/Resources/` dumping ground;
- framebuffers owning unrelated render-target lifetime;
- automatic dependency/Vulkan/VMA upgrades during unrelated work;
- duplicate resource systems beside `ResourceManager`;
- speculative RenderGraph infrastructure hidden inside current pass code;
- two active SceneRenderers when only one is selected;
- large "cleanup" rewrites mixed with feature implementation.

## 20. Current Strategic Direction

Use this as architectural context, not as permission to implement everything at once.

The intended progression is approximately:

```text
Stable renderer architecture
    -> logical passes
    -> Dynamic Rendering
    -> shared PreDepth / Shadow / Forward / Composite resources
    -> cleaner draw-stream based submission
    -> hybrid deferred renderer
    -> RenderGraph
    -> automatic barriers/lifetimes/transient resources
```

The exact order may change if the codebase makes another dependency more sensible, but each step should remain independently understandable and testable.

## 21. Claude Code Decision Checklist

Before making a significant Cerberus change, answer internally:

1. What is the exact responsibility being changed?
2. Which layer owns it today?
3. Which layer should own it after the change?
4. Is this state persistent or just an operation on data?
5. Am I adding abstraction because it is required, or only because it looks cleaner?
6. Does this change alter GPU execution, synchronization, or lifetime?
7. Can this be split into a smaller compileable CL?
8. Am I accidentally solving a future RenderGraph problem prematurely?
9. Have I searched for duplicate/legacy call sites?
10. How will I validate that behavior and output are still correct?

If the answer to ownership or lifetime is unclear, inspect more code before editing.

---

**Default principle:** keep Cerberus explicit today in a way that makes it easier for RenderGraph to automate tomorrow.
