# Unified Next Action Agent Architecture for KiSurf

## Executive synthesis

The strongest long-term design for KiSurf is **one unified Next Action Agent loop** with **mode-specific provider stacks**, not three separate systems for placement, routing, and auto-filling. The loop should always follow the same high-level sequence: **observe context → classify work state → generate constrained candidates → run internal trial-and-review on a rollbackable shadow state → publish at most one user-facing preview → accept through the standard typed-op path or silently expire**. This recommendation fits the runtime direction already documented in the KiSurf branch: the branch centers on a Python-first session runtime, typed atomic operations, a semantic shadow board, checkpoints, rollback, validation, preview rendering, and accept replay through one `BOARD_COMMIT`, while the current Background Preview Agent is intentionally preview-only and read-only in the first phase. citeturn8view1turn8view2turn8view3turn9view0turn9view2

For KiSurf specifically, the key design move is to treat **everything the agent uses as tools**, including queries, observations, candidate generators, route planners, validators, preview renderers, checkpoint/rollback, and accept replay. That is consistent with the branch spec’s move away from old composite model-facing tools toward a typed session vocabulary with session control, query/observation, and mutation operations. citeturn9view3turn9view4turn8view2

The most important product distinction is not “which agent can call which code,” but **workflow policy**. Chat Agent and Next Action Agent should share the same low-level substrate, but Chat Agent is a user-directed execution surface that can spend more latency budget and walk longer edit plans, while Next Action Agent is a **low-latency, low-interruption, high-cancellation preview surface** that should usually stay silent unless it has a high-confidence, inspectable recommendation. That separation mirrors how GitHub Copilot distinguishes ask, edit, and agent modes, how JetBrains separates next edit suggestions from agent mode, and how Cursor treats “Tab” as a distinct next-action product rather than a general autonomous agent. citeturn40view6turn41view7turn40view5turn40view2turn40view3

My core recommendation is therefore:

**Build one Background Suggestion Manager above the existing session runtime and below the user-visible preview surface.**
It should own context signatures, cancellation, candidate arbitration, self-review budgets, preview publication, and stale/supersede handling, while delegating execution to the same session and typed-op infrastructure already being built for KiSurf. citeturn18view0turn17view1turn8view1turn8view2

## Recommended unified architecture

The right architecture is a **single agent loop with a shared suggestion object model** and **different providers per work state**. The loop should not branch into three separate systems early. Instead, it should keep these pieces common across all states:

- a shared **context signature**
- a shared **candidate schema**
- a shared **trial session / rollback model**
- a shared **preview publication pipeline**
- a shared **accept / reject / expire / supersede state machine**
- a shared **telemetry vocabulary**

This aligns well with the KiSurf research note in the selected branch, which argues for a layered hybrid architecture: deterministic providers for strong-constraint, low-ambiguity suggestions; algorithmic candidate generators for feasible geometric options; and a low-latency ranker to decide whether to surface anything at all. That note explicitly recommends normalizing provider outputs into one candidate schema and rendering those candidates as typed preview plans rather than live board mutations. citeturn13view0turn13view1turn17view0

### What the unified loop should do

At every meaningful editor event, the loop should:

1. **Snapshot the context signature.**
   The minimum stable signature should include board revision, selection revision, view revision, tool-state hash, and focused widget/panel revision where relevant. The KiSurf state-machine research already recommends separating board, selection, view, and tool-state revision domains, and the session spec already enforces base-hash and selection-conflict checks. citeturn18view0turn9view1turn8view4

2. **Classify the current work state.**
   The classification should be functional, not product-level. Typical states are:
   - geometric placement continuation
   - geometric routing continuation
   - structured fill / refill
   - passive observation with no recommendation
   - strategic hint only, with no auto-preview
   This is exactly how your request frames the problem, and it is the right decomposition.

3. **Fan out to providers in parallel.**
   Deterministic providers, geometry generators, router-backed candidate generators, and lightweight ranking/publish policy can run in parallel. The KiSurf research note explicitly recommends “parallel generation, unified arbitration,” and VS Code’s provider model and cancellation semantics support the same engineering pattern for fast invalidation and re-generation. citeturn13view0turn43view0

4. **Open a constrained internal trial session when geometry matters.**
   Placement and routing candidates should be tested in a rollbackable shadow session, not guessed once and shown directly. KiSurf’s runtime spec already gives you the right primitives: checkpoints, rollback, shadow-board mutation, validation, preview rendering, and accept replay to the live board only after explicit accept. citeturn8view1turn8view2turn8view3

5. **Evaluate candidates with a publish gate, not only model confidence.**
   A suggestion should publish only if it is:
   - semantically relevant
   - fast to inspect
   - likely acceptable
   - low enough in interruption cost
   - still valid under the current context signature
   This is directly supported by Cursor’s public emphasis on confidence-gated display and accept-rate control, and by VS Code’s UX for collapsed NES rather than always showing the entire edit. citeturn41view4turn41view5turn41view2turn40view1

### How Chat Agent and Next Action Agent should share infrastructure

They should share:

- session open / close
- typed atomic ops
- shadow-board execution
- observation and query tools
- preview rendering
- validation ladder
- checkpoint / rollback
- accept replay into one live-board transaction

They should differ in policy:

| Dimension | Chat Agent | Next Action Agent |
|---|---|---|
| Initiation | User-invoked | Ambient / event-driven |
| Latency budget | Higher | Strict |
| Session scope | Broader | Narrow and local |
| Mutation freedom | Can run richer edit sessions | Constrained preview sessions only |
| Output form | Conversational explanation + preview | Hint / overlay preview first |
| Interruptiveness | Explicitly entered | Must default to silence |
| Acceptance | Explicit accept after proposal | Explicit accept, never autonomous apply |

This is consistent with KiSurf’s branch spec, which currently keeps the Background Preview Agent autonomous but preview-only, and refuses unrestricted mutation sessions for it until the session core is proven. citeturn9view0turn9view2

## Self-review loop and rollbackable internal attempts

For **placement and routing**, the agent should not jump from inference directly to user-visible preview. It should run a bounded self-review loop inside a shadow environment. The right mental model is not “LLM reflection” alone, but **tool-interactive critique**: propose, execute in simulation, observe, validate, critique, repair, and either publish or discard. That is much closer to CRITIC-style external-feedback refinement than to unconstrained self-talk, and it is also directionally aligned with work like Self-Refine and Reflexion, which are useful mainly as a reminder that iterative improvement works best when grounded in explicit feedback signals. citeturn20search0turn19search0turn19search1

### Recommended internal loop

For any candidate that changes geometry:

**Observe**
Query the current board region, selected objects, nets, layers, design rules, viewport, and recent activity. KiSurf’s typed session vocabulary already defines this class of query and observation operations. citeturn9view3turn9view4

**Attempt**
Apply a candidate to the shadow board only, optionally after creating a checkpoint. The KiSurf runtime already records mutation ops against the shadow board and keeps the live board untouched until accept. citeturn8view2turn8view3

**Observe the result**
The branch spec is especially important here: it says each step should produce three synchronized observations—structured diff and changed handles, validation result and warnings, and a native preview frame or preview handle. That is exactly the right contract for internal self-review. citeturn8view1

**Critique**
Run a validation ladder:
- typed argument validation
- semantic validation
- geometry validation
- incremental connectivity / ratsnest update
- selective refill / DRC-lite
- full DRC only when needed
KiSurf’s spec already defines this layered validation ladder, and explicitly states that refill should be a controlled validation phase, not an unconditional side effect on every tiny step. citeturn9view0turn8view2

**Repair or rollback**
If the result is poor, either locally modify the candidate or rollback to the checkpoint and try the next candidate. KiSurf already defines checkpoint content, rollback semantics, stale handle handling, preview restoration, and the guarantee that rollback leaves the live board untouched. citeturn8view1turn8view2

**Publish only if good enough**
The publish threshold should be “best within the latency budget and with no hard failures,” not “globally optimal.” In a real editor, predictable timing beats heroic search.

### What “good enough” should mean

A candidate is “good enough” when:

- **hard constraints pass** or the failure is explicitly communicated and still useful
- **the candidate is locally coherent** with current user intent
- **the preview is visually inspectable**
- **the latency is within budget**
- **the context signature is still current**

In practice, I would recommend the following budgets:

- **Placement continuation:** usually 2–4 internal attempts inside roughly 150–350 ms after a short stability dwell
- **Routing continuation:** usually 1–3 internal attempts inside roughly 80–250 ms, because routing interruptions feel worse than delayed placement hints
- **Auto-filling / refilling:** often zero geometric attempts; mostly one inference plus schema consistency checks

Those exact numbers are design recommendations rather than upstream facts, but they follow directly from the public evidence that next-action systems win by publishing selectively and by keeping latency low: Cursor emphasizes sufficient-confidence gating and suppressing noisy predictions, while Cursor’s “Fusion” Tab model reports a p50 latency of 260 ms for its editor workflow, which is a useful real-world anchor for what an always-on next-action surface can tolerate. citeturn41view4turn41view5turn42view0turn42view1

### Should internal attempts be visible?

**Usually no.**
The user-facing product should almost always show the **final reviewed preview only**, not the self-review process. That is especially true for routing and placement, where flickering failed trials would steal attention and undermine trust. VS Code’s long-distance NES explicitly moved to a compact, lightweight preview because showing too much or jumping unnecessarily hurts trust, and Cursor’s public discussion likewise frames low-quality suggestions as distracting noise. citeturn41view2turn41view4

There are only a few cases where exposing process helps:

- the user explicitly expands “alternatives”
- there are two or three materially different route corridors worth comparing
- the system wants to explain why it refused to show a stronger suggestion
- an expert/debug mode is enabled

The default should still be: **hide internal attempts, show final preview**.

## Preview layering, safety boundaries, and stale handling

The cleanest design is to use **one preview infrastructure with two visibility tiers**:

- **internal attempt preview**, visible only to the agent / system
- **user-facing preview**, visible to the engineer

That matches KiSurf’s current session runtime direction, where preview is already a formal subsystem bridging the shadow board to native KIGFX preview infrastructure, with provenance metadata, checkpoint restoration, and clearing on rollback/reject/cancel/close/accept. citeturn8view1turn8view2

### How to avoid polluting the live board

The answer is straightforward and high confidence because the branch spec already points to it:

- apply all trial mutations to the **shadow board**
- render previews from **shadow-board state**
- keep **observation and preview records** in the journal for audit
- replay only **mutation / maintenance records** to the live board on accept
- perform accept in **one `BOARD_COMMIT::Push()` transaction** so undo is coherent citeturn8view1turn8view2turn8view3turn38view0

That is the correct long-term contract for “preview-first, auditable, rollbackable” AI editing in a deeply integrated editor.

### Recommended suggestion state model

KiSurf’s own next-action state-machine research is already close to the right design. I recommend using:

- **Pending**: candidate under evaluation, not shown
- **HintOnly**: lightweight cue exists, but full preview is not painted
- **Previewing**: single visible overlay is armed for accept
- **Accepted**: handed off to typed-op accept replay
- **Rejected**: explicitly dismissed or clearly acted against
- **Expired**: context drifted; not a user rejection
- **Superseded**: replaced by a newer candidate in the same group citeturn18view0

On top of that, keep **three validity layers** per suggestion:

- **render-valid**: can be drawn correctly
- **semantic-valid**: still matches the user’s apparent task
- **accept-valid**: can still be promoted without recomputation

That distinction matters. A suggestion may remain semantically useful after a viewport pan, while no longer being renderable in the current view; and it may remain renderable after a board edit, while no longer being safe to accept because its dependencies changed. KiSurf’s research document makes exactly this distinction, and it is the right one. citeturn17view1turn18view0

### Recommended invalidation rules

Each suggestion should bind to a context signature containing at least:

- board revision
- selection revision
- view revision
- tool-state hash
- focused UI widget revision for panel/table states

Then invalidate like this:

- **board revision change touching dependencies** → expire immediately
- **selection change for selection-scoped suggestion** → expire immediately
- **view change** → usually collapse from Previewing to HintOnly rather than expire
- **tool-state incompatibility** → lose precedence immediately or expire
- **newer stronger candidate in same context group** → supersede
- **in-flight attempt sees cancellation signal** → abandon attempt, do not patch old result forward

This is strongly supported by KiSurf’s state research, VS Code’s cancellation-token model for long-running suggestion work, and KiSurf’s own selection-conflict and base-hash enforcement in the session runtime. citeturn18view0turn43view0turn9view1turn8view4

### Interaction recommendation

For acceptance, the safest pattern is:

- `Tab` from HintOnly reveals / focuses the preview
- `Tab` again accepts if the preview is armed
- clicking the overlay focuses it
- clicking an explicit accept chip accepts
- `Esc` dismisses
- panning away or switching context collapses to HintOnly or expires

That pattern maps well to Copilot NES’s “Tab to navigate, Tab to accept,” while respecting that PCB canvas clicks already carry heavy native meaning. citeturn40view1turn13view5

## Placement mode

Placement is the best place to make the self-review pattern explicit, because placement quality is only partially about legality. It is also about **layout quality**: component affinity, ratsnest quality, congestion, symmetry, mechanical constraints, and readability.

### How the agent should infer intent in placement

The Next Action Agent should infer placement intent from a combination of:

- selected footprint or footprint family
- recent move/drag/select history
- connected nets and ratsnest anchors
- local congestion and keepout boundaries
- room / functional cluster context
- explicit mechanical constraints and board-edge relations
- symmetry / pairing patterns
- active tool and viewport region

The agent should not begin from “guess the final `(x, y, angle)`.” It should begin from **reference structures**. KiCad’s own placement UX is a good hint here: it distinguishes the footprint anchor, pad centers, move-with-reference, position-relative, and interactive offset workflows. That tells you the editor already thinks in terms of anchors and reference points, not raw pixel placement. citeturn43view1

### Recommended placement self-review flow

A strong placement flow looks like this:

**Intent extraction**
Infer “what is being placed relative to what.” Examples:
- “place this decoupler near U5 power pins”
- “nudge this connector inward to satisfy edge clearance”
- “continue placing stitching vias along this zone edge”
- “place this differential pair protection structure symmetrically”

**Candidate generation**
Generate several anchor-based candidates, not one coordinate. Candidate families should come from:
- nearest relevant pad clusters
- symmetry transforms
- edge/keepout offsets
- room centroids or cluster barycenters
- local grid-snapped legal sites
- topology-derived alignments with already placed peer parts

**Shadow-board trial**
Apply a candidate footprint transform to the shadow board.

**Observation and scoring**
Observe:
- overlap / collision
- keepout and courtyard interference
- DRC-lite failures
- ratsnest delta
- estimated congestion cost
- mechanical margin
- pair/symmetry deviation
- distance to the intended reference items

**Repair**
If poor, try local repair:
- small translation search
- rotation variants
- mirrored or paired transform
- edge-offset adjustment
- different site in same candidate family

**Publish**
Show only the final candidate unless the user explicitly asks for alternatives.

### What to expose to the agent

For placement, the most durable tool abstraction is **not** a huge matrix of hardcoded tools like `place_decoupler`, `align_to_connector`, `fix_overlap`, and so on. Those are useful as experimental helpers, but they should not become the core interface.

The stable abstractions should be:

- query spatial/topological context
- generate candidate anchors / regions
- apply a placement transform in shadow state
- observe structure + geometry + violations + ratsnest delta
- checkpoint / rollback
- render preview
- accept / reject

That gives the agent enough compositional power without hardcoding every placement macro.

### What the final preview should look like

For placement, the final preview should usually include:

- translucent footprint body
- optional ratsnest deltas or tether lines
- conflict/warning annotations only when they affect the decision
- light callout text such as “closer to source,” “reduces ratsnest,” or “violates keepout” only when needed

KiCad’s graphics model already separates durable content from transient preview content, and KiSurf’s session spec is already building provenance-aware preview groups on top of that. citeturn18view0turn8view1

## Routing mode and abstraction-layer comparison

Routing is the hardest case, and it is the one where architectural discipline matters most. Mature EDA systems already point in the right direction here: the engineer provides intent, constraints, or route guides, and the routing system searches the legal solution space. KiCad’s router supports interactive routing, nearest-ratsnest finishing, multiple routing modes, differential-pair routing, and length/skew workflows; Altium’s ActiveRoute explicitly positions itself as a **guided interactive router**, not a general autorouter, and emphasizes route guides, spacing, meander, length tuning, and pin swapping under design-rule control. citeturn29view0turn36search6turn36search9turn43view2

### The long-term recommendation

The best long-term design is a **layered routing interface**:

- **high level for the model:** routing intent
- **mid level for control and explainability:** corridor / anchor / candidate path selection
- **low level for execution:** router / solver and typed geometry ops

The model should rarely author concrete coordinates directly. Instead, it should specify:
- route start / end anchors
- target net or net group
- preferred layers
- whether vias are allowed
- style goals such as shortest, cleaner, fewer vias, tighter coupling, bus parallelism, or return-path friendliness
- optional corridor / route guide
- acceptance priorities among candidates

Then deterministic routing tools should generate and validate concrete path candidates in shadow state.

### Detailed comparison of routing abstractions

| Abstraction | What the agent manipulates | Strengths | Weaknesses | Long-term recommendation |
|---|---|---|---|---|
| Low-level coordinate / pixel style | Raw points, segments, bends, vias | Maximal expressive control | Brittle, hard to validate early, poor generalization, poor UX explainability, too much token pressure | Keep only as an internal or expert/debug layer |
| Anchor / reference-point style | Start/end anchors, corners, obstacle-relative targets | Better semantic stability, easier recovery, maps to editor concepts | Still pushes too much geometric search burden onto the agent | Good mid-level primitive |
| Candidate path selection | Choose among 2–N pre-generated legal routes | Strong explainability, bounded search, easy ranking and repair | Requires a good candidate generator | Excellent default publication layer |
| Intent-driven routing | Specify net, goal, style, corridor, layer policy | Best model ergonomics and long-term extensibility | Needs a deterministic compiler/solver underneath | Best primary agent-facing abstraction |
| Router-as-tool | Call underlying router / planner with constraints | Stable, legal, fast, leverages mature algorithms | Needs careful wrapping or it becomes opaque and hard to steer | Essential execution layer |

The most future-proof interface is therefore **intent → candidate routes → router-backed realization → validation → preview**.

### Why not let the agent drive coordinates directly?

Because routing quality depends on legality, topology, and style together. KiCad’s router class alone already exposes concepts like routing mode, visible view area, start/move/finish, layer switching, via placement, updated items, current nets, routing settings, and nearest ratsnest anchors. That is evidence that the underlying routing problem is already structured and stateful; it should be wrapped as a tool ecosystem, not flattened into token-by-token coordinate generation. citeturn29view0turn29view1

### Recommended routing self-review flow

**Intent extraction**
Infer whether the user likely wants:
- continue current track
- finish to nearest anchor
- fanout
- differential pair continuation
- bus continuation
- dodge obstacle / repair local clearance
- preserve corridor / style

**Planner invocation**
Use router-backed tools to produce a small candidate set. Candidate generation can be seeded by:
- current cursor / end anchor
- ratsnest target
- allowed layers
- diff-pair/bus constraints
- local route guide corridor
- via budget
- style preset

**Shadow routing attempt**
Run the route in the shadow board.

**Observation**
Inspect:
- clearance violations
- connectivity outcome
- layer changes and via count
- excessive detour
- bus parallelism / diff coupling quality
- interference with likely future routes
- local congestion footprint
- whether the route matches apparent user directionality

**Repair**
Try:
- alternate corridor
- different layer preference
- via allowance change
- softer / tighter constraint mode
- shorter finish-to-anchor variant
- different candidate from the candidate set

**Publish**
Present one primary preview, optionally with an affordance for “alternatives.”

### How the final route preview should appear

The safest and most legible pattern is:

- show the proposed route as transient overlay
- mark changed or newly occupied space
- annotate only the salient tradeoff:
  - “fewer vias”
  - “keeps pair tighter”
  - “avoids keepout”
  - “finishes to nearest anchor”
- if off-screen or remote, show a compact hint rather than paint a large inline diff immediately

That recommendation is supported both by VS Code’s compact long-distance NES widget and by CAD/EDA precedent that previews should be explicit, inspectable, and separate from commit. Autodesk Fusion’s `executePreview` / `isValidResult` design is especially useful here because it formalizes the principle that preview computation can be reused as final result only after explicit confirmation. citeturn41view2turn43view3

## Auto-filling and refilling mode

Auto-filling / refilling is part of the same Next Action Agent, but it is a meaningfully simpler state because the problem is less geometric and more schema- and context-driven.

### What the agent should detect

The agent should recognize when the user is currently editing:

- a property panel
- a table
- a rules form
- a dialog with structured fields
- a schematic/PCB property cluster that supports copy-down or inferred defaults

This is the exact kind of suggestion that should default to a **deterministic provider first**, because the target space is small and strongly typed. The KiSurf next-action research note already argues that structured value completion, width inheritance, via-size defaults, bus spacing, layer values, and similar completions should come primarily from schema/rule providers rather than from free-form LLM generation. citeturn13view0turn17view0

### How self-review differs from geometric self-review

For auto-fill, self-review is usually not spatial trial-and-rollback. Instead, it is:

- schema validation
- enum / range validation
- cross-field consistency checking
- project-context consistency
- object-family consistency
- optional explanation generation

So yes, there is still self-review, but it is more like **consistency review** than **geometry repair**.

### Recommended fill flow

**Observe**
Read the focused field, panel schema, selected object(s), relevant rule/naming/value context, and recent related edits.

**Infer**
Propose one or a few candidate values or value bundles.

**Review**
Check:
- type correctness
- allowed value range
- cross-field compatibility
- consistency with project conventions
- whether the suggestion would now be stale because the user typed again or changed selection

**Publish**
Show as inline ghost values, panel chips, or “fill these three related fields” suggestions.

**Accept / reject**
Accept should write through the same auditable typed-op path where practical, or at least through a typed UI edit record. Reject should suppress repetition for the local context.

### Staleness handling for filling

For fill/refill, stale detection should be driven by:

- focused widget revision
- object revision
- schema revision
- selection revision
- whether the user continued typing in the same field

This is one area where the agent can be more aggressive, because the cost of showing and withdrawing a small panel suggestion is much lower than the cost of flashing a route preview on the canvas.

## Tool abstraction principles and implementation guidance

The tool design question is the most important long-term issue in your prompt. The answer is not a fixed list of tools. It is a set of interface principles.

### Stable long-term principles

A capability should be exposed as a reusable agent tool when it is:

- **typed** enough to validate arguments before action
- **observable** enough to return structured results
- **reversible** enough to rollback or sandbox
- **previewable** enough to render before commit
- **auditable** enough to leave a journal trail
- **composable** enough to combine with other tools
- **policy-separable** enough that Chat and Next Action can use the same primitive under different budgets

Those properties are already strongly reflected in the KiSurf session runtime spec. citeturn8view1turn8view2turn9view3

### Stable interfaces versus experimental interfaces

**Good stable interfaces**
- session lifecycle
- context query / observation
- candidate application to shadow state
- checkpoint / rollback
- validation ladder
- preview rendering
- accept replay
- typed object/property transforms
- router / solver invocation under typed constraints

**Good experimental interfaces**
- domain-specific pattern generators
- learned ranking features
- style priors
- repair heuristics
- special-case placement templates
- alternative route-corridor generators

The rule of thumb is simple: keep the stable layer **semantically narrow but compositionally rich**. Put fast-evolving heuristics and solved patterns above it.

### How to avoid hardcoding every combined action

Do not build the public interface around compound actions like:

- `place_decoupler_near_u5`
- `continue_diffpair_around_usb_connector`
- `fill_bus_spacing_defaults`

Those are valuable as libraries or provider modules, but they should lower into a smaller set of core abstractions:
- observe
- generate candidates
- apply in shadow state
- validate
- render
- rollback
- accept

This is the same reason the KiSurf branch explicitly moves composite helpers out of the model-facing tool surface and into a lowerable SDK/composite library. citeturn9view3

### A concrete implementation path for KiSurf

The most pragmatic path is phased.

**Near term**
Keep the Background Preview Agent mostly preview-only as the branch currently intends, but build the full suggestion manager and state machine now. Even if early suggestions are conservative, the architecture will be correct. citeturn9view0turn9view2

**Next phase**
Allow **constrained preview sessions** for geometric Next Action:
- strict latency budgets
- strict candidate count budgets
- no autonomous accept
- aggressive cancellation
- local-region scope only

**Later phase**
Introduce learned ranking and publish policies once enough accept/reject/expire/supersede telemetry is available.

That sequence preserves product safety and lets the session runtime mature before you let the ambient agent use richer mutation-backed previews.

## Final recommendation

The most durable answer for KiSurf is:

**Build one Next Action Agent, not three.**
But make it a **policy-governed, work-state-aware suggestion loop** above a **single typed session substrate**.

The agent should:

- understand placement, routing, and auto-fill as different **work states**, not different products
- bind every suggestion to a **context signature**
- use **parallel providers** and one **unified candidate schema**
- run geometric suggestions through **shadow-board trial, observation, validation, repair, and rollback**
- reuse the same native preview stack for internal and external previews, but with **different visibility tiers**
- publish only a **single primary preview** at a time
- separate **Expired** from **Rejected** and **Superseded**
- accept only through the same **typed-op → one `BOARD_COMMIT`** path used elsewhere
- keep low-level coordinate control available internally, but make **intent + candidate-path + router-backed execution** the long-term default interface for routing
- treat auto-filling as the same architecture with a lighter-weight, schema-driven self-review path

In short: the right KiSurf architecture is **not** “LLM guesses the next geometry.” It is **an editor-native, tool-using, rollback-capable suggestion system** whose unit of output is a **validated candidate operation bundle with a native preview**, exactly in line with the branch’s “suggest, accept, materialize” direction. citeturn17view0turn13view1turn8view1turn8view2turn8view3

## Open questions and limitations

This report is grounded in the selected KiSurf branch’s published spec and research docs plus official external documentation from KiCad, VS Code/Copilot, Cursor, JetBrains, Autodesk, and Altium. However, I was not able to exhaustively inspect the entire KiSurf source tree through connector-native code search during this pass, so some recommendations are architectural rather than tied to every current implementation file. citeturn7view0turn12view0turn12view1

The most important open implementation questions are:

- whether Next Action should own its own constrained session pool or borrow from a shared session service
- how much of KiCad’s existing PNS router should be wrapped directly versus re-expressed through KiSurf semantic shadow items
- whether user-visible “alternatives” should be a first-class UI affordance in routing and placement, or deferred to an expert mode
- how far accept-valid previews can be reused directly without recomputation in all geometric cases, especially when validation includes selective refill or external DRC dependencies

Those are solvable design questions, but they should be resolved after the **Background Suggestion Manager**, **context signature model**, and **trial/review/publish pipeline** are in place.