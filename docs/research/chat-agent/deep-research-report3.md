# Native Preview Architecture for an AI Agent Inside KiCad

## Executive conclusion

KiCad already contains most of the rendering and editor primitives needed to build a true **preview-first AI agent** inside the PCB editor. The current architecture separates **display objects** from **board mutations**: the GAL/KIGFX stack renders `VIEW_ITEM` objects inside a `KIGFX::VIEW`, while editor mutations are finalized through change-tracking and undo-aware commit code such as `BOARD_COMMIT::Push()` and can be reverted with `BOARD_COMMIT::Revert()`. The existing codebase also already uses multiple preview patterns: direct preview groups, immediate-mode overlays, interactive router preview items, selection overlays, measurement rulers, ratsnest visuals, and DRC marker layers. citeturn24view3turn23view6turn17view1turn38view0turn39view1turn26search3turn28search5

For an AI Native PCB editor, the strongest long-term design is **not** “let the AI mutate the real `BOARD` and hope undo saves us,” and also **not** “show only a fake external canvas.” The best architecture is a **two-plane model**:

1. a **shadow editing plane** that applies typed operations to a shadow board and runs validation, and  
2. a **native preview display plane** that renders the current shadow result into the same KiCad `KIGFX::VIEW` the user is already using.  

On Accept, replay or materialize the validated operation journal into the real board through one KiCad-native commit so the result lands cleanly in the undo stack. On Reject or rollback, discard back to a checkpoint in the shadow session and regenerate the preview scene. That approach is the one that best matches both KiCad’s current internals and your AI-agent requirements for step execution, visual inspection, rollback, replay, and user-visible progress. citeturn23view6turn31view5turn35view7turn38view0turn39view1turn29search3

Because no public KiSurf repository was available in this research, the analysis below is grounded in current upstream KiCad source and developer documentation, then extended into a KiSurf-oriented design recommendation.

## What KiCad already does for preview and overlay rendering

KiCad’s rendering core is the **GAL** and **KIGFX** system. The official developer documentation describes the GAL tool framework as the mechanism that made KiCad’s interactive editors more flexible and easier to extend than the old legacy canvas. At the code level, `KIGFX::VIEW` is the object that “holds a potentially large number of `VIEW_ITEM`s and renders them on a graphics device provided by the GAL,” and its public API includes explicit support for making overlays and preview groups. citeturn29search3turn24view3

The key abstraction is `VIEW_ITEM`. KiCad board items, preview items, router preview objects, and overlay objects all implement the same view contract: they provide a bounding box, a set of layers they draw on, and a draw function. In the KiCad APIs, the rendering system repeatedly calls `ViewDraw()` for each layer returned by `ViewGetLayers()`, using the layer ordering inside the `VIEW`. That is exactly the extension seam an AI preview system should reuse instead of inventing a second canvas. citeturn17view2turn17view1turn28search4

KiCad has **two native preview mechanisms** already:

The first mechanism is a **preview group** managed by `KIGFX::VIEW` itself. `VIEW::InitPreview()` creates a `VIEW_GROUP`, adds it to the main view, and `VIEW::AddToPreview()` inserts a preview item into that group, makes the preview visible, and updates it. `VIEW::ClearPreview()` clears that group, deletes owned preview items, and updates the view; `ShowPreview()` toggles the group’s visibility. This is the clearest built-in precedent for “temporary visible geometry that is not yet a committed board mutation.” citeturn23view6turn23view4turn23view3

The second mechanism is `VIEW_OVERLAY`, which is a special `VIEW_ITEM` that stores a command list of immediate-mode primitives such as lines, segments, circles, arcs, rectangles, polygons, polylines, bitmap text, and draw-style settings like fill, stroke, and line width. KiCad creates one with `VIEW::MakeOverlay()`, which constructs a `VIEW_OVERLAY` and adds it to the view. This is a native way to draw transient editor decorations and annotations without creating full board-like objects. citeturn24view0turn37view0

The source tree also shows several specialized preview item classes built on top of those abstractions. `KIGFX::PREVIEW::SIMPLE_OVERLAY_ITEM` is a reusable base for preview graphics: it is an `EDA_ITEM`, implements `ViewGetLayers()` and `ViewDraw()`, exposes fill/stroke/line width settings, and delegates the actual geometry drawing to `drawPreviewShape()`. `SELECTION_AREA` derives from that base and uses `LAYER_GP_OVERLAY` for its selection rectangle or lasso polygon, which shows that editor interactions already render structured temporary graphics on dedicated overlay layers. citeturn16view0turn17view2turn17view1

The measurement tool is another useful precedent. In `pcb_viewer_tools.cpp`, KiCad creates a `KIGFX::PREVIEW::RULER_ITEM`, adds it directly to the view, then toggles visibility during interaction rather than committing it to the board. That is exactly the lifecycle an AI “step preview” should follow: create temporary visual entities, show them while the interaction is live, and remove them without polluting the permanent model unless explicitly accepted. citeturn27view1

KiCad also reserves many layers specifically for temporary or informational graphics. `layer_ids.h` includes entries for **ratsnest**, **auxiliary items**, **DRC warning/error/exclusion/highlight** layers, **marker shadows**, **locked-item shadows**, **conflict shadows**, and `LAYER_SELECT_OVERLAY`, which is documented as the “Selected items overlay.” That means KiCad is already architected around the idea that not everything users see on the canvas is a persistent `BOARD_ITEM`. citeturn26search3turn28search2turn28search5turn35view2

## The concrete classes and control flow that matter most

For an AI-integrated editor, the most important native classes are `KIGFX::VIEW`, `VIEW_ITEM`, `VIEW_GROUP`, `VIEW_OVERLAY`, the PCB tool framework, and `BOARD_COMMIT`.

`KIGFX::VIEW` is the scenegraph-like container. Its API surface is already suggestive: it can `Add()` items, `Update()` them, create overlays with `MakeOverlay()`, initialize a preview group with `InitPreview()`, place items into that preview group with `AddToPreview()`, clear the preview group with `ClearPreview()`, temporarily hide items with `Hide()`, and query visible items spatially. The developer docs also position the GAL tool framework as the right home for interactive editor behavior. That combination is important: rendering, interaction, and temporary editor state are already meant to be handled inside the native tool/view system, not from an external plugin drawing over the top. citeturn24view3turn24view0turn23view4turn29search3

`VIEW_GROUP` is KiCad’s native grouping unit for multiple `VIEW_ITEM`s. The class list describes it as extending `VIEW_ITEM` with the ability to group items into a single object, and KiCad uses that pattern in its preview infrastructure and router interface. That makes `VIEW_GROUP` the best native anchor for an **AI step group**, a **checkpoint group**, or a **session group**. citeturn11search1turn23view6

The interactive router is the strongest precedent for the kind of “AI preview” you want. In `PNS_KICAD_IFACE::SetView()`, KiCad stores the view pointer, constructs a `KIGFX::VIEW_GROUP`, assigns it to `LAYER_SELECT_OVERLAY`, and adds the group to the main view. During preview display, `PNS_KICAD_IFACE::DisplayItem()` constructs `ROUTER_PREVIEW_ITEM` objects and adds them into that preview group, then updates the view. The debug decorator uses the same pattern: it creates translucent `ROUTER_PREVIEW_ITEM`s from geometric shapes, adds them to a view group, updates the view, and even manages depth to avoid translucent shapes overwriting each other incorrectly. This is almost a direct template for an AI preview renderer. citeturn35view1turn35view7turn35view6turn35view8turn32view3turn32view2

`ROUTER_PREVIEW_ITEM` itself is instructive. It is an `EDA_ITEM`, exposes `ViewBBox()`, `ViewDraw()`, and `ViewGetLayers()`, stores color, width, clearance, depth, and layer information, and is designed to render geometric routing previews rather than committed board data. Its existence is evidence that KiCad’s native preview path is not limited to simplistic outlines; it can represent board-like geometry with layer semantics, depth ordering, and constraint visualization. citeturn16view2turn17view0

On the mutation side, `BOARD_COMMIT` is the editor’s native commit-and-undo integration point. `BOARD_COMMIT::Push()` collects staged changes, updates the view and connectivity, handles ratsnest and teardrop side effects, emits selection/tool events, and ends up feeding the undo machinery. `BOARD_COMMIT::Revert()` is the native rollback path for staged board changes. In other words, KiCad already distinguishes **staging**, **push-to-undo**, and **revert**. That is exactly the semantic boundary an AI system should preserve. citeturn38view0turn38view3turn38view4turn39view1turn39view4

## What this implies for an AI Preview Agent design

The source strongly suggests that your AI preview system should be split into **authoritative preview state** and **renderable preview state**, rather than trying to make the render layer itself authoritative.

The authoritative side should be a **shadow board session** owned by the AI runtime. That shadow session should apply typed atomic operations, maintain checkpoint metadata, keep object-handle remapping, run geometry validation and rule checks, and generate a journal of accepted preview steps. That design is not directly in upstream KiCad as a single class, but it fits the same staged-before-push model already embodied in `BOARD_COMMIT`. citeturn38view0turn39view1

The render side should be an **AI preview scene** inside the native `KIGFX::VIEW`. The important design choice is that the visible preview should be derived from the shadow state, not be the source of truth itself. That keeps DRC/constraint checking, rollback, and final Accept logically separate from the canvas. KiCad’s own code already treats preview groups and overlays as non-authoritative display objects, while `BOARD_COMMIT` handles authoritative edits. citeturn23view6turn24view0turn38view0

A key architectural choice is whether AI-generated preview geometry should be rendered as `VIEW_OVERLAY` commands or as custom `VIEW_ITEM`s grouped in a `VIEW_GROUP`.

For **persistent, inspectable, step-by-step AI construction**, the better default is **custom `VIEW_ITEM`s in a `VIEW_GROUP`**, not a pure `VIEW_OVERLAY`. The reason is structural. `VIEW_OVERLAY` is fundamentally a command buffer of primitive draw calls; it is ideal for annotations, arrows, halos, guide text, and temporary helper geometry. By contrast, custom `VIEW_ITEM`s can carry object identity, source operation IDs, per-item metadata, per-item depth, and layer semantics in the same way that `ROUTER_PREVIEW_ITEM` already does. That makes them far better suited for preview vias, preview tracks, preview copper shapes, preview clearance outlines, hoverable error badges, and click-to-inspect journal provenance. This is a design inference, but it is tightly grounded in how the two KiCad mechanisms are implemented. citeturn37view0turn17view0turn35view6

So the recommended rendering split is:

- **AI preview geometry**: custom `VIEW_ITEM`s, usually batched inside `VIEW_GROUP`s.
- **AI annotations**: `VIEW_OVERLAY`, for labels, leader lines, arrows, “why I did this” hints, and ephemeral cursor-following decorations.
- **AI session preview group**: a top-level `VIEW_GROUP` under the main `KIGFX::VIEW`.
- **Optional per-step groups**: child groups or logical subgroups to enable timeline scrubbing and checkpoint visibility switching. citeturn23view6turn24view0turn11search1

## Recommended long-term display architecture

The strongest long-term design is an **AI Preview Layer subsystem** inside KiCad/KiSurf, implemented natively in the editor core.

A practical class model would look like this:

```text
AI_AGENT_SESSION
  ├─ AI_SHADOW_BOARD
  ├─ AI_OPERATION_JOURNAL
  ├─ AI_CHECKPOINT_MANAGER
  ├─ AI_PREVIEW_SCENE
  │    ├─ AI_PREVIEW_SESSION_GROUP
  │    ├─ AI_PREVIEW_STEP_GROUPS
  │    ├─ AI_PREVIEW_BATCH_ITEM
  │    ├─ AI_PREVIEW_ZONE_ITEM
  │    ├─ AI_PREVIEW_VIA_BATCH_ITEM
  │    ├─ AI_PREVIEW_TRACK_BATCH_ITEM
  │    └─ AI_PREVIEW_OVERLAY
  └─ AI_ACCEPT_APPLIER
```

The **most important boundary** is this: every prospective board mutation should first become a **typed atomic operation** in the journal, then a change on the shadow board, then a corresponding preview scene update, and only after user Accept should it become a real `BOARD_ITEM` mutation committed with `BOARD_COMMIT`. That preserves auditability, rollback safety, and native undo semantics. It also prevents the preview from ever “polluting” the real board. This is the same separation KiCad already keeps between preview display objects and pushed board edits. citeturn23view6turn38view0turn39view1

### How the render layers should be organized

I do **not** recommend reusing `LAYER_SELECT_OVERLAY` as the permanent long-term home for AI visuals, even though the router already uses it and it is fine for an MVP. The long-term architecture should add dedicated KiSurf/Editor-native AI overlay layers such as:

- `LAYER_AI_PREVIEW_GEOM`
- `LAYER_AI_PREVIEW_LABELS`
- `LAYER_AI_PREVIEW_WARNINGS`
- `LAYER_AI_PREVIEW_GHOSTS`
- `LAYER_AI_PREVIEW_CHECKPOINTS`

The reason is not that KiCad cannot reuse existing overlay layers; it clearly can. The reason is that an always-on Preview Agent and a chat-driven construction Agent need independent visibility, styling, filtering, and hit policies from selection, ratsnest, DRC markers, and router preview. KiCad already uses many specialized non-board layers for these concerns, so adding a dedicated AI family is consistent with the existing layer model. citeturn26search3turn28search2turn28search5turn35view2

### How preview items should carry provenance

Every AI preview object should carry at least:

- `session_id`
- `step_id`
- `checkpoint_id`
- `op_id`
- `source_handle`
- `shadow_object_id`
- `preview_style`
- `validation_status`

That metadata should not live only in the journal. It should also be attached to the preview item or batch entry so the UI can offer hover cards such as “Step 4: place via stitching ring,” “derived from op 128,” or “failed hole-to-hole constraint.” This is not spelled out in upstream KiCad, but it is the natural extension of the per-item model used by preview items and board items in the existing view layer. citeturn17view0turn16view2

### How hover and click should work

If you need rich inspection of preview items, prefer **preview item objects with stable IDs** over a single overlay command list. The view system already supports rectangular item queries and item-layer rendering contracts; using itemized preview objects makes it much easier to map a click back to a specific pending operation. In practice, I would keep AI preview interaction separate from ordinary board selection so that clicking a preview item invokes an AI preview inspector rather than mutating the user’s PCB selection state. That recommendation follows from the fact that KiCad’s view and selection systems are already tool-mediated rather than magically inferred from draw commands. citeturn24view0turn29search3

### How to keep performance acceptable

KiCad’s `VIEW` is designed to hold a potentially large number of `VIEW_ITEM`s, but for AI-generated previews such as thousands of stitching vias or many regenerated track variants, you should still avoid one heap object per tiny primitive when possible. The best long-term pattern is a **batched `AI_PREVIEW_BATCH_ITEM`** for homogeneous geometry types, with an internal vector of per-instance metadata and an auxiliary hit index for picking. This is an engineering recommendation rather than an upstream KiCad rule, but it follows from how the view system groups, layers, and updates renderable items. citeturn24view3turn11search1

## How Preview, Accept, Reject, Rollback, and Replay should work

The cleanest Preview/Accept flow is:

**Script step executes → typed ops are appended → shadow board mutates → validators run → preview scene regenerates or updates incrementally → user and agent inspect → Accept or Rollback.**

That flow maps naturally onto KiCad’s existing concepts. The preview stays in the view layer; the real board stays untouched until final promotion; and the final promotion is one native commit. citeturn23view6turn38view0turn39view1

### Preview phase

During preview execution, the AI runtime should update only the **shadow board** and the **AI preview scene**. It should **not** create real `BOARD_ITEM`s on the live board during exploratory execution. Existing KiCad preview code already shows temporary graphics being added to preview groups, overlays, or temporary view items without becoming durable board state. citeturn23view6turn24view0turn27view1turn35view6

After each AI step, perform three outputs:

- a structured **board delta** for the agent,
- a rendered **native visual preview** for the user and agent,
- a **validation report** containing DRC-like, geometry, selection, or rule feedback.

That combines the user-visible timeline you want with the machine-readable loop the agent needs to repair its own script.

### Accept phase

On Accept, do **not** commit “whatever preview items currently exist.” Instead, convert from the **typed operation journal** or shadow board diff into real board mutations, then push them via a single `BOARD_COMMIT::Push()` with one user-facing commit message. This lands the whole accepted AI action in the native KiCad undo stack as one coherent undo step, which is exactly what engineers expect. `BOARD_COMMIT::Push()` already handles view updates, connectivity, ratsnest maintenance, and related editor side effects. citeturn38view0turn38view3turn39view4

### Reject phase

Reject should be cheap: clear the AI preview scene, dispose of the shadow session, and leave the real board untouched. KiCad’s own preview path already treats preview content as disposable: `VIEW::ClearPreview()` clears the preview group and deletes owned items. citeturn23view6

### Rollback phase

Rollback should operate at **checkpoint** granularity, not at arbitrary renderer granularity. A checkpoint should minimally capture:

- operation journal index,
- shadow board base hash or equivalent snapshot reference,
- preview object ID mapping,
- script-visible handle remapping state,
- validation cache state if any.

When rolling back, reset the shadow board to the checkpoint, invalidate subsequent preview groups, and regenerate the current visible preview scene from the restored shadow state. This aligns conceptually with `BOARD_COMMIT::Revert()`, but your AI session should use it as a design precedent rather than literally reverting the real board during preview. citeturn39view1

### Replay phase

Replay should be a first-class feature. Because the operation journal drives both the shadow board and the visible preview, replay becomes deterministic and valuable: users can scrub step-by-step through what the AI did, and the background Preview Agent can re-open a failed checkpoint, adjust parameters, and continue. That is the natural consequence of keeping the journal authoritative and the preview derivative. The source supports the editor side of that split, even though the journal itself is your new subsystem. citeturn23view6turn38view0

## Concrete implementation recommendation

### The long-term stable architecture

The best long-term architecture is:

**Typed operation journal + shadow board + native KIGFX AI preview scene + final BOARD_COMMIT promotion**

That is the architecture least likely to force a rewrite later, because it keeps the most stable boundaries aligned with KiCad’s existing internal seams:

- **Rendering boundary**: `KIGFX::VIEW`, `VIEW_ITEM`, `VIEW_GROUP`, `VIEW_OVERLAY`
- **Interaction boundary**: GAL tool framework and PCB tool classes
- **Mutation boundary**: staged changes and `BOARD_COMMIT`
- **Undo boundary**: single push into KiCad undo stack on final Accept citeturn29search3turn24view3turn23view6turn38view0turn39view1

### What I would add to KiCad or KiSurf

You likely need a small set of new native classes:

`AI_PREVIEW_MANAGER` should own the session-level preview lifecycle and attach to `PCB_EDIT_FRAME` / `PCB_DRAW_PANEL_GAL` / `KIGFX::VIEW`.

`AI_PREVIEW_ITEM` should be a base `VIEW_ITEM` carrying provenance metadata.

`AI_PREVIEW_BATCH_ITEM` should render many vias, tracks, or markers efficiently.

`AI_PREVIEW_OVERLAY` should wrap `VIEW_OVERLAY` for labels and annotation lines.

`AI_SHADOW_SESSION` should own the shadow board, journal, checkpoints, and validation pipeline.

`AI_ACCEPT_APPLIER` should take the accepted journal and promote it into one `BOARD_COMMIT`.

Those classes are consistent with the upstream design patterns already used by router preview, selection preview, ruler preview, and staged board commits. citeturn17view1turn27view1turn35view1turn38view0

### The best MVP

A strong first MVP would be intentionally narrow:

Use one **AI session preview `VIEW_GROUP`** added to the current `KIGFX::VIEW`.

Render preview vias, tracks, polygons, and labels as custom preview items on **`LAYER_SELECT_OVERLAY` initially**, because it is already a valid preview overlay layer in editor practice, even if you later replace it with dedicated AI layers. citeturn35view7turn35view2

Keep an **operation journal plus shadow board**, but initially support only:
create/move/delete via, track, zone outline, and shape.

Support **step checkpoints** only at tool-defined boundaries, not every atomic op, so rollback logic stays manageable.

On Accept, replay the journal into the real board and finalize with **one `BOARD_COMMIT::Push()`** so the result appears as one undoable edit. citeturn38view0turn39view1

That MVP is fully aligned with the current source architecture and does not paint you into a corner. Later, you can add dedicated AI layers, batched preview items, richer hover inspection, timeline scrubbing, and background Preview Agent suggestions without changing the fundamental layering model.

## Final recommendation

KiCad’s existing internals point to a very clear answer: treat AI preview as a **native editor resident**, not as an external overlay and not as direct board mutation.

Use the **shadow board** for truth, the **typed journal** for audit/replay/checkpoints, and the **KIGFX view system** for preview display. Render meaningful AI geometry as custom `VIEW_ITEM`s grouped in `VIEW_GROUP`s, and reserve `VIEW_OVERLAY` for lightweight annotations. Promote to the real board only on Accept, using one native `BOARD_COMMIT` so undo, ratsnest updates, connectivity, and other editor side effects remain fully KiCad-native. citeturn24view0turn23view6turn35view1turn35view6turn38view0turn39view1

If you adopt that architecture, you get the properties your AI Native PCB editor actually needs: step execution, visual inspection in the real canvas, checkpoint rollback, replay, clean undo integration, rich provenance, and a display layer that is extensible enough for both a chat-driven construction agent and a continuously observing Preview Agent. citeturn29search3turn24view3turn38view0turn39view1