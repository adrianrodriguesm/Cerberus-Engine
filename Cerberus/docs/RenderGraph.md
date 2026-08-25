# Cerberus RenderGraph

Last updated: 2026-08-25 (increment 2)

This document explains `RenderGraph` (`Cerberus/src/Renderer/RenderGraph/RenderGraph.h`/`.cpp`):
what problem it solves, how it's implemented, what it buys the engine today, and
what it deliberately does not do yet. It complements
[`RendererArchitecture.md`](RendererArchitecture.md), which covers the graph in
the context of the rest of the renderer; this document goes deeper on the graph
itself.

## Why

Before this, every scene renderer hand-sequenced its own Dynamic Rendering
scopes and hand-wrote the barrier that opened each one. `ForwardSceneRenderer`
called `GraphicsCommandList::BeginRendering`/`EndRendering` directly, once per
logical stage, and `BeginRendering` inserted the *same* barrier every time
regardless of what actually happened before — it had no memory of which pass
last touched a resource, so it always covered the union of every case that
could possibly be true ("this could be a previous frame's shader-read, or it
could be another pass's attachment write reused via `LOAD_OP::LOAD`") rather
than the one that actually was true. `PathTracerSceneRenderer` had the same
problem in miniature: one hand-written barrier for its accumulation image,
correct only because someone reasoned through it by hand and it happened to
stay correct as the code around it changed.

That approach doesn't scale past a handful of passes, and it actively worked
against the direction Cerberus is heading: a hybrid deferred renderer with
more passes, more shared resources, and eventually async compute, all of
which need barriers that are *derived*, not maintained by hand at every call
site.

`RenderGraph` fixes this by making resource access something a pass
*declares* instead of something a call site works out for itself. The graph
sees every pass's declared reads and writes for the whole frame before any of
them run, so it can order passes from real dependencies and compute the exact
barrier each access needs — not a guess that happens to be safe.

**Increment 1** was pass ordering and precise, per-resource barriers.
**Increment 2** (this update) added automatic Dynamic Rendering scope merging,
pass culling, and a debug/GPU-timing panel — three smaller pieces picked from
increment 1's "What's missing" list because they had real value without
needing anything the graph doesn't already have. Transient/aliased resource
allocation was investigated for increment 2 and explicitly deferred instead:
Cerberus's current resources have no real aliasing opportunity yet (see "What's
missing" below), so that work waits until the deferred renderer's post-process
chain gives it real candidates.

## How it works

### The three-phase lifecycle

Like Unreal Engine 5's RDG, Frostbite's FrameGraph, and Granite's Vulkan
render graph — all three were studied before designing this — `RenderGraph`
follows a Setup → Compile → Execute lifecycle:

1. **Setup**: the `SceneRenderer` imports every resource it'll touch this
   frame, then registers passes. Each pass gets a *setup* callback (declares
   what it reads/writes) and an *execute* callback (records the actual work).
   Nothing runs yet.
2. **Compile**: the graph builds a dependency graph from every pass's declared
   accesses, culls anything unreachable from a pass that writes an imported
   resource, topologically sorts the survivors, resolves the exact barrier
   each access needs from that order, and marks consecutive graphics passes
   that share their full attachment set as mergeable.
3. **Execute**: the graph replays the compiled order — a group's barriers,
   then that group's pass(es), one GPU timestamp-query pair per pass — into
   the frame's command buffer. A "group" is one pass, or a merged run sharing
   one Dynamic Rendering scope.

### Core types

```cpp
// Per-graph, per-frame resource handle. Not the same as
// Handle<RenderTargetTexture>/Handle<Image>: it indexes the graph's own
// access-history table, not ResourceManager's generational pools.
struct RGTextureHandle { uint32_t Index; bool IsValid() const; };

// What memory became visible, from which pipeline stage(s), after an access.
// Image layout isn't tracked here — Image already tracks its own current
// layout (m_CurrentImgLayout), the same way non-graph code relies on it.
struct RGAccessState { VkAccessFlags Access; VkPipelineStageFlags Stage; };

enum class RGAccessKind
{
    ReadTexture, WriteColorAttachment, WriteDepthAttachment, ReadWriteStorageImage
};
```

### Importing resources

Every resource the graph touches this frame is registered once, up front, on
the graph itself — not inside a pass's setup:

```cpp
RGTextureHandle sceneColor = graph.ImportRenderTarget(m_SceneColor, "SceneColor");
RGTextureHandle sceneDepth = graph.ImportRenderTarget(m_SceneDepth, "SceneDepth");
```

This matters because the same underlying resource is usually touched by
several passes — PreDepth and Forward both write `SceneDepth`; Forward and
Composite both touch `SceneColor` — and every pass that touches it must use
the *same* handle so the graph recognizes them as the same resource. A
`RenderGraphBuilder` (the object passed into a pass's setup callback) is
scoped to just that one pass, so importing from inside it would hand out a
fresh, unrelated handle to each pass and silently break dependency tracking.
`ImportRenderTarget` infers whether a target is a color or depth resource
from its format, and computes its conservative first-touch barrier state
accordingly (see "Barrier resolution" below). `ImportImage` is the same for a
plain `Handle<Image>` (used by `PathTracerSceneRenderer`'s storage images),
and additionally takes an explicit `initialAccess` — a storage image's
first-touch state isn't inferable from an attachment kind, so the caller
supplies it.

### Registering passes

```cpp
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
```

`AddGraphicsPass`/`AddComputePass`/`AddRayTracingPass` mirror Cerberus's three
typed command-list domains (a fourth, `TransferCommandList`, isn't graph-driven
yet — nothing currently needs it to be). The setup lambda runs immediately
(it just records declarations into the pass), the execute lambda runs later,
during `Execute()`.

This lambda-based builder is a deliberate, explicit exception to the "no
closures, no virtual interfaces" convention the Dynamic Rendering migration
otherwise follows. It was chosen because it's what every major engine's graph
API actually looks like, and it keeps a pass's resource declarations next to
the code that uses them instead of in a separate table someone has to keep in
sync by hand.

### Compile(): ordering

`Compile()` builds a dependency graph from every pass's declared accesses: a
pass depends on every earlier pass that wrote a resource it reads or writes
(read-after-write, write-after-write), and on every earlier reader of a
resource it writes (write-after-read). It then runs Kahn's algorithm over
that dependency graph, breaking ties by registration order.

The tie-breaking matters in practice: the four shadow-cascade passes each
write a *different* render target, so nothing in the dependency graph orders
them relative to each other — without a deterministic tie-break they could
come out in any order. Breaking ties by registration order means they render
in cascade order (0, 1, 2, 3) exactly as they were added, which is also
exactly what the old hand-sequenced code did. The topological sort isn't
decorative here — it's what makes that ordering an emergent property of the
declared dependencies instead of something the graph would need to be told
separately.

### Compile(): barrier resolution

This is the actual payoff. Once the execution order is known, `Compile()`
walks every resource across that order, tracking its current
`RGAccessState`, and for each access records:

- **`Before`**: the state the resource was actually left in by whatever
  touched it last *this frame* (or its imported first-touch state, for a
  resource's first access).
- **`After`**: what this access leaves visible for whatever touches the
  resource next.

A resource's first touch within a frame still uses a conservative,
dual-purpose state — the graph has no visibility into what happened *last*
frame either, so this is the same reasoning `BeginRendering` used to apply
everywhere, just computed once instead of on every call. What changes is
every access *after* the first: instead of re-deriving that same
conservative guess, the barrier is built from the *exact* producer access and
stage the previous pass actually recorded. Concretely, PreDepth writing
`SceneDepth` now tells Forward's later depth-attachment access exactly
"`DEPTH_STENCIL_ATTACHMENT_WRITE_BIT` at `LATE_FRAGMENT_TESTS`" instead of
Forward re-deriving the old union of "could be a previous frame's read, could
be same-frame reuse."

### Compile(): pass culling

A pass is a **root** if any of its declared accesses writes something —
today that always means writing an *imported* (externally-owned) resource,
since the graph has no way yet to create a resource of its own that a write
could target instead (see "What's missing"). `Compile()` walks backward from
every root through the already-computed dependency edges (a pass's
`DependsOnPasses` already points at exactly what it needs, so a forward walk
from the roots through that edge set gives the full closure of passes
required to produce every root's output) and marks anything not reached as
**culled**: excluded from the topological sort entirely, so it gets no
barriers and never runs.

Because every resource is imported today, every pass is a root, and culling
currently removes nothing — the same "correct but currently inert" shape
increment 1's barrier-precision work had at a resource's first touch. It
becomes load-bearing the moment a transient, graph-created resource exists
that a pass can produce without anything depending on it.

### Compile(): scope merging

Two consecutive passes in the compiled order merge into one Dynamic Rendering
scope when they're both `Pass::Type::Graphics` and declare the *exact same*
set of color/depth attachment resources. Equality here is transitive, so a
run longer than two passes merges correctly from pairwise checks alone — no
extra bookkeeping needed to detect a longer chain.

This isn't just a recording optimization: a `vkCmdPipelineBarrier` cannot
legally target an image that is currently bound as an active Dynamic
Rendering attachment, so once two logical passes are declared as *separate*
graph passes (rather than one pass whose execute callback happens to call two
render functions), something has to guarantee their barriers land before the
scope opens rather than between them. Detecting the merge and grouping the
barriers accordingly is that guarantee — see "Execute(): recording" below for
where it actually happens.

### Execute(): recording

`Execute()` replays the compiled order into the frame's command buffer — one
`BeginRenderThread`/`EndRenderThread`/`SubmitRenderThread` at the call site,
exactly as before the graph existed; there's no per-pass submission. It walks
the execution order in **groups** — a group is one pass, or a run of
consecutive passes `Compile()` marked mergeable — and for each group:

1. Inserts *every group member's* every declared access's resolved barrier
   (`Image::InsertImageMemoryBarrier`, the same call non-graph code already
   uses) before anything in the group runs. This is required, not just
   convenient: barriers can't be recorded once the group's scope is open.
2. For a graphics group with attachment writes, builds **one** `RenderingInfo`
   for the whole group — a resource declared by more than one member (Sky and
   Forward both target `SceneColor`) appears once, taking its `LoadOp` from
   the first member that declared it and its `StoreOp` from the last — and
   opens the scope through `GraphicsCommandList::BeginRenderingAttachments`,
   the same `vkCmdBeginRendering` call `BeginRendering` makes, but without
   inserting a barrier itself, since `Execute()` already recorded the
   graph-resolved ones. Width/height come from an attachment's own
   `RenderTargetTexture`.
3. Invokes each group member's execute callback in order, with the
   appropriate typed command list, each wrapped in its own GPU timestamp
   query pair (`CommandBuffer::BeginTimestampQueryRenderThread`/
   `EndTimestampQueryRenderThread` — see "Debugging" below) so a merged
   group's members still get individually attributed GPU time.
4. Closes the scope once with `EndRenderingAttachments` after the last group
   member (again, no barrier — the *next* group's barriers, resolved in
   `Compile()`, handle that).

After every group has run, any shader-resource render target nothing in the
graph read back — Forward's `CompositeOutput`, sampled later by the editor
viewport outside this graph entirely — gets one closing transition to a
shader-read layout, mirroring what `EndRendering` used to do unconditionally
for every shader-resource attachment when its scope closed.

### Debugging: the RenderGraph panel

`RenderGraph::OnImGuiRender(CommandBuffer&)` draws a "RenderGraph" section in
each `SceneRenderer`'s existing "Renderer" ImGui window: a table of every
compiled pass (name, type, whether it merged with the previous pass, GPU time
from its timestamp query), an expandable per-pass list of declared accesses
with the exact `Before`/`After` barrier `Compile()` resolved for it, and a
list of anything culled. It reuses `CommandBuffer`'s existing (previously
unused by real rendering) timestamp-query API — see the paragraph below.

GPU-timestamp results lag the frame that recorded them by roughly one
submission cycle (`CommandBuffer::SubmitInternal` collects the *previous*
completed use of a frame-in-flight slot's query pool, not the one just
submitted) — acceptable for a debug overlay.

Building this surfaced a real, pre-existing bug: `CommandBuffer`'s original
`BeginTimestampQuery`/`EndTimestampQuery` are "queued" API (their
`vkCmdWriteTimestamp` is deferred through `RenderQueueManager`, meant for
callers on the application thread before `Begin()` is even queued). Calling
them from `RenderGraph::Execute()` — which runs on the render thread, already
mid-recording via `BeginRenderThread()` — deferred the write past that same
recording's `EndRenderThread()`/`SubmitRenderThread()`, producing
`VUID-vkCmdWriteTimestamp-commandBuffer-recording`. The fix added immediate
counterparts, `BeginTimestampQueryRenderThread`/`EndTimestampQueryRenderThread`,
matching the naming convention `CommandBuffer` already uses for this exact
queued-vs-immediate distinction (`Begin`/`BeginRenderThread`, etc.). It also
uncovered that `BeginRenderThread`/`EndRenderThread`/`SubmitRenderThread` —
the path both scene renderers actually render through — passed
`enableQueries`/`collectQueryResults = false` throughout, meaning the
timestamp *and* pipeline-statistics query pools were never reset, written, or
read back on that path at all. Flipping those three flags to `true` was
necessary for per-pass timing to work and, as a side effect, made
`CommandBuffer::GetPipelineStatistics()` return real data for the first time
on the path real rendering uses.

### The whole pipeline, end to end

`ForwardSceneRenderer::Render()` registers six passes every frame:

```text
PreDepth          writes SceneDepth (CLEAR)
Shadow x N        one pass per cascade, each writes its own shadow map (CLEAR)
Sky               writes SceneColor (CLEAR), writes SceneDepth (LOAD)
ForwardGeometry   writes SceneColor (LOAD), writes SceneDepth (LOAD), reads
                  every shadow map -- merges with Sky (identical attachment set)
Composite         reads SceneColor, writes CompositeOutput
```

Sky and Forward are registered as genuinely separate passes now — each
describes only what it actually touches (Sky never reads a shadow map;
increment 1's single hand-merged `"ForwardOpaque"` pass had to declare the
shadow-map reads for both, since nothing distinguished which of the two
callbacks it invoked actually used them) — and `Compile()`/`Execute()`
recover the single shared Dynamic Rendering scope automatically, from the
scope-merging logic above.

`PathTracerSceneRenderer::FlushRenderPasses()` registers one ray-tracing pass
wrapping its `TraceRays` dispatch, declaring `ReadWriteStorageImage` on the
accumulation image. That single declaration replaces the hand-written barrier
that used to sit next to the dispatch call.

## Benefits

- **Correctness moves from "someone reasoned through it" to "derived from
  declared data."** The barrier for any access is a function of what was
  actually declared before it in the same frame, not a hand-maintained
  invariant that has to stay correct as passes are added, removed, or
  reordered around it.
- **Ordering is an emergent property of dependencies, not a maintained
  sequence.** The four shadow cascades don't need anyone to know or state
  that they're independent — the graph discovers that from the fact that
  they touch disjoint resources. Adding a fifth pass that only reads
  `SceneColor` doesn't require auditing where in the call sequence it needs
  to go; it needs to go wherever it's registered, and the graph resolves the
  rest.
- **Barriers got strictly more precise, not just relocated.** The
  within-frame reuse cases (PreDepth → Forward's `SceneDepth`, Forward →
  Composite's `SceneColor`) now get the exact producer access/stage instead
  of the old always-conservative union. This is real, measurable
  synchronization tightening, not just a refactor that moved the same code
  elsewhere.
- **One mental model for both scene renderers.** `ForwardSceneRenderer` and
  `PathTracerSceneRenderer` used to have two unrelated approaches to
  synchronization — one hand-sequenced Dynamic Rendering scopes, the other
  hand-wrote a single barrier next to its dispatch. Both now describe their
  per-frame work the same way, through the same API, even though their
  actual pass structures (five passes vs. one) are very different.
- **The pass list is now inspectable data, not implicit control flow.** What
  a pass reads and writes is declared explicitly, in one place, instead of
  being inferable only by reading `BeginRendering`/`EndRendering` call sites
  and cross-referencing which resources happen to be bound. This is what
  makes the deferred work below tractable later without another rewrite —
  culling, scope-merging, and transient aliasing all want exactly this
  declared-access data, which now already exists.
- **No behavior change, by design.** Neither increment changes a single pixel
  of output — they change how the existing barriers, ordering, and Dynamic
  Rendering scopes get produced. That was a hard constraint during
  implementation (see `RendererArchitecture.md`'s validation checklist)
  precisely so the graph could evolve without also being a rendering-behavior
  change to debug at the same time.
- **Splitting a pass no longer costs a hand-written merge.** Sky and Forward
  went from "one pass because they must share a scope" back to "two passes,
  because that's what they actually are," without losing the shared scope —
  the graph recovers it from the fact that they touch the same attachments.
  Anyone adding the next pass that needs to share a scope with something
  gets this for free instead of having to notice and hand-merge it.
- **A registered-but-dead pass now costs nothing, automatically.** Culling
  doesn't remove anything from Cerberus's graph today (see "What's missing"),
  but it's real: a future authoring mistake — registering a pass whose output
  nothing reads — gets pruned and surfaced in the debug panel instead of
  silently costing GPU time forever.
- **The graph is now inspectable at runtime, not just in source.** Before this,
  answering "what order did the passes actually run in," "did Sky and Forward
  actually share a scope," or "why does this pass have that barrier" meant
  reading `RenderGraph.cpp`. The debug panel answers all three from the
  running frame, which is what actually made it possible to keep validating
  this work by eye instead of by re-deriving it from source every time.

## What's missing

Automatic scope merging, pass culling, and the debug/timing panel — all
"missing" as of increment 1 — shipped in increment 2 (above). What's left,
still explicitly out of scope rather than an oversight:

- **Transient/aliased resource allocation.** The graph doesn't own render
  target creation/destruction — every imported resource is still a
  `Handle<RenderTargetTexture>`/`Handle<Image>` the owning `SceneRenderer`
  creates and destroys itself, exactly as before the graph existed.
  Investigated directly for increment 2 and deferred on its merits, not on
  sequencing: Cerberus's current Forward resources (SceneColor, SceneDepth,
  four shadow maps, CompositeOutput) have no real aliasing opportunity today
  — everything ends up alive simultaneously by the time `ForwardGeometry`
  reads the shadow maps and writes `SceneColor`/`SceneDepth` together, and no
  two resources share both format and dimensions with a non-overlapping
  lifetime regardless. The payoff shows up once the deferred renderer's
  post-process chain (GBuffer, bloom, blur, TAA history) gives the graph
  resources that actually don't overlap.
- **Async compute / cross-queue scheduling.** Still gated on explicit
  queue-family ownership tracking, independent of the graph — see
  `RendererArchitecture.md`'s "Compute policy before RenderGraph" section.
  The graph's dependency data would be exactly what a scheduler needs to
  decide what can run concurrently on a second queue, but that scheduler
  doesn't exist yet.
- **Cycle diagnostics.** A dependency cycle (which shouldn't be
  constructible from Cerberus's actual pass graph today) currently trips a
  `CORE_ASSERT` in `Compile()` rather than reporting which passes/resources
  form the cycle. Fine for now; worth improving once the graph has enough
  real users that a cycle becomes a plausible authoring mistake rather than
  a theoretical one.
- **`TransferCommandList` isn't graph-driven.** Uploads, copies, and mip
  generation still go through `TransientCommandExecutor` outside the graph
  entirely, which is correct for now (that's setup/asset-processing work,
  not per-frame rendering — see `RendererArchitecture.md`), but a future
  frame-scoped transfer pass (e.g. streaming) would need a fourth `AddPass`
  variant.
- **The debug panel's culling/merging views are permanently uninteresting
  today.** It correctly shows zero culled passes and one merged pair, because
  that's the whole current graph's actual shape — it'll start earning its
  keep once the graph is bigger. Worth remembering when reading it, so an
  empty culled list isn't mistaken for the feature being untested rather than
  the graph genuinely having nothing to cull yet.
