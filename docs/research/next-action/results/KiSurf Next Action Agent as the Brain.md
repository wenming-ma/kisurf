# KiSurf Next Action Agent as the Brain

## Core recommendation

The strongest long-term architecture for KiSurf is **one unified Next Action Agent loop** that owns suggestion policy, tool orchestration, self-review, and preview publication across placement, routing, and auto-filling, while delegating heavy computation to tools that remain subordinate to the Agent rather than becoming independent decision systems. That recommendation fits both the product vision in the current branch and the direction already encoded in the branch’s specs: KiSurf explicitly positions AI as a first-class in-editor collaborator, separates Chat Agent and Next Action Agent by interaction style rather than by core infrastructure, and already has preview/accept/reject foundations, explicit tool-state snapshots, workspace-context state, preview-only background suggestions, and a shadow-board/session-runtime architecture for reversible work. fileciteturn1file0L7-L13 fileciteturn1file0L24-L27 fileciteturn1file0L54-L92 fileciteturn2file0L13-L31 fileciteturn42file0L14-L29

The key architectural stance should be:

```text
observe current workspace
→ classify current work state
→ decide whether intervention is warranted
→ call candidate/validation/query/planning tools
→ run bounded internal attempts in scratch state
→ self-review against intent + rules + geometry + UX constraints
→ publish only the final user-visible preview
→ commit only after explicit Accept
```

That stance is also consistent with the broader agent literature. ReAct argues that high-quality agents interleave reasoning and action, rather than treating tool results as automatic answers. Self-Refine, Reflexion, Tree of Thoughts, and step-level verification work all show that performance improves when a system can generate, critique, and revise intermediate steps instead of trusting the first draft or only evaluating the final outcome. For KiSurf, that means **placement and routing should be attempt-based**, not one-shot guessed outputs. citeturn7academia0turn0academia1turn0academia0turn7academia1turn8academia0

My bottom-line recommendation is therefore:

* **One Agent, many work states**
* **One context model, many domain tools**
* **One preview-first commit boundary**
* **Hidden internal attempts, visible final preview**
* **Deterministic/native fast paths on hot interaction loops**
* **Model-backed reasoning only on debounced or semantically meaningful triggers**

That last point is especially important because the KiSurf branch already states that the model should **not** be called on every cursor motion, and that high-frequency events should rely on native deterministic providers while model-backed suggestions run on coarser triggers. fileciteturn6file0L80-L93

## What the current KiSurf branch already establishes

The current branch is already much closer to the desired architecture than a greenfield design. The README says KiSurf’s core loop is “suggest, accept, materialize,” and that the Next Action Agent is intended for “placement continuation, routing continuation, anchor-based route previews, and other contextual continue-from-here previews,” with suggestions tied to editor revision, selection, tool state, and viewport so stale suggestions can expire. It also states that the current phase already has preview-only Next Action plumbing and a Python-first session runtime with checkpoints, rollback, native preview rendering, validation, and accept replay. fileciteturn1file0L29-L53 fileciteturn1file0L73-L92 fileciteturn2file0L3-L18

The June 17 design spec is even more explicit. It adds **tool-state snapshots**, **per-context background workspace state**, **low-latency Next Action Preview**, typed semantic command tools, and a “native-first” architecture where the editor process remains the source of truth for preview lifetime, router state, undo/commit integration, and stale-context expiration. The spec also distinguishes shared context from mode-specific context, with tool states such as `RoutingTrack`, `PlacingVia`, `PlacingFootprint`, `DrawingZone`, and `MovingSelection`, and workspace contexts such as `Routing`, `ViaPlacement`, `FootprintPlacement`, `ZoneCreation`, and `SelectionEdit`. fileciteturn5file0L16-L25 fileciteturn5file0L96-L124 fileciteturn5file0L128-L191

The implementation aligns with that spec. The current default provider chain in `AI_AGENT_PANEL_MODEL` is:

1. deterministic via-pattern provider
2. deterministic routing-segment provider
3. deterministic panel-table provider
4. model-backed `AI_AGENT_SUGGESTION_PROVIDER`

That ordering matters. It already encodes a healthy product principle: **cheap, native, explainable providers should get first shot**, and model reasoning should be above them, not below them, in the final policy stack. fileciteturn19file0L11-L20

The current controller is also unified, not split into separate mini-agents. `AI_NEXT_ACTION_CONTROLLER` holds a vector of providers, iterates through them, and deduplicates repeated suggestions by fingerprint, context version, and tool state. The orchestrator gives suggestions a lifecycle of `Pending`, `Previewing`, `Accepted`, `Rejected`, or `Expired`, supports preview and accept through dedicated manager/session interfaces, and expires active suggestions when the context version changes. fileciteturn13file0L43-L64 fileciteturn17file0L123-L162 fileciteturn40file0L10-L48 fileciteturn22file0L193-L236 fileciteturn23file0L15-L36 fileciteturn23file0L39-L96

That is the right foundation, but it is still **candidate-first**, not yet fully **Agent-self-review-first**. Today, deterministic providers mainly emit a single suggestion candidate and the orchestrator manages lifecycle. My recommendation is to preserve the single-controller architecture, but upgrade the provider contract from “return one suggestion” toward “return candidate bundles plus evidence and validation, which the Agent can inspect and refine before publishing.”

## Recommended unified brain architecture

### Responsibility boundary

To keep the Next Action Agent as the “brain,” four classes of decisions should stay with the Agent:

The Agent should decide **whether to intervene at all**, **which work-state interpretation is active**, **which tools to call and in what order**, and **whether a tool result is good enough to become a user-visible preview**. It should also decide whether to retry, repair, or silently drop a bad attempt. Those are the core policy decisions that determine whether KiSurf feels like a coherent collaborator instead of a bag of automations. That design follows the repo’s own “candidate, not commit” posture and the broader ReAct-style argument that acting and intermediate evaluation should be interleaved rather than hardwired into tool outputs. fileciteturn6file0L80-L110 fileciteturn1file0L106-L109 citeturn7academia0turn7academia2

The tools, in contrast, should own **specialized computation**: context extraction, candidate generation, router planning, local placement solving, DRC queries, clearance checks, ratsnest delta metrics, panel schema parsing, hidden preview rendering, rollback, and geometry materialization. KiSurf’s session-runtime spec already argues for exactly this kind of tool surface: the model should see session-level and observation tools rather than an ever-growing list of bespoke composite commands, and composite helpers should lower into typed atomic operations instead of bypassing the core. fileciteturn42file0L57-L95

A useful litmus test is simple:

* If the question is **“what should we try next?”**, that belongs to the Agent.
* If the question is **“compute/validate/render/compare this proposed thing”**, that belongs to a tool.

### The right control model

I recommend moving from the current simple provider chain to a **brain-centered control loop with structured tool bundles**:

```text
Sensing layer
  context snapshot + tool state + anchors + panel state + activity + viewport

Policy layer
  work-state classifier + interruptibility gate + intervention policy

Attempt layer
  candidate generation + scratch edit + observe + validate + compare + repair

Publication layer
  final preview + rationale summary + accept/reject affordances + stale binding

Commit layer
  revalidate + apply through native commit + audit trail + cleanup
```

This is still a single Next Action Agent, but it makes the hidden attempt loop explicit. The branch already has almost all of the pieces: one shared context carrier (`AI_CONTEXT_SNAPSHOT`), explicit `AI_TOOL_STATE_SNAPSHOT`, semantic anchors, panel states, preview manager, suggestion orchestrator, and the session/shadow-board architecture for reversible edits and accept replay. fileciteturn39file0L167-L262 fileciteturn24file0L23-L100 fileciteturn42file0L20-L29 fileciteturn42file0L147-L185

### The tool contract the Agent actually needs

The most important tool-design principle is not a fixed list of verbs, but a **reviewable return shape**. For Next Action work, tools should not just return “best candidate” or a scalar score. They should return enough information for the Agent to inspect and compare. In practice, that means every tool output should tend to include six things:

* the **proposed delta** or preview artifact
* the **assumptions** used to generate it
* the **context/version binding**
* the **validation summary** and failure reasons
* the **provenance** needed for rollback/audit
* lightweight **observation hooks** so the Agent can inspect the result after the attempt

KiSurf’s session journal and preview manager already point in this direction. The journal is supposed to record typed arguments, created handles, warnings, validation summaries, and structured results, while preview objects carry provenance metadata such as session id, step id, checkpoint id, operation id, preview style, and validation status. fileciteturn42file0L163-L185 fileciteturn43file0L1-L20

The right design move is therefore **not** “build one router tool that directly decides the route,” but rather “build route planning, route scoring, route validation, route observation, route render, and route accept boundaries that the Agent can compose.” The same principle applies to placement and auto-fill.

## Self-review and preview layering

### Internal attempts should be hidden by default

For placement and routing, intermediate attempts should normally **not** be shown to the engineer. The engineer wants a coherent final suggestion, not the agent’s search trace. This is also consistent with proactive-assistant research, which emphasizes that proactive systems are useful only when they intervene at the right time and in the right way; excessive visible interruption quickly becomes distracting or annoying. KiSurf’s own observability work already treats internal traces, tool calls, and suggestion lifecycle as a separate log surface rather than the main workspace surface. citeturn13academia0 fileciteturn8file0L7-L10

My recommendation is:

* **Default mode:** only show the final preview.
* **Debug mode / expert mode:** allow the engineer to inspect the attempt history, validation overlays, and agent rationale in a log or expandable “why this suggestion” card.
* **Exception mode:** show progressive process only when the process itself is useful, such as a fanout planner offering two or three candidate breakouts, or a route planner showing mutually exclusive options the engineer may want to choose between.

That gives you the benefits of self-review without UI flicker.

### Internal preview and user-visible preview should share infrastructure, but not identity

KiSurf should use **one preview substrate** for hidden attempts and final previews, but assign different provenance and visibility semantics.

The current preview manager already supports batches of preview items and overlays, and the session-runtime spec explicitly calls for provenance metadata and clearing preview on rollback, reject, cancel, close, or accept. That is exactly the right substrate for a layered preview system. fileciteturn24file0L23-L100 fileciteturn43file0L6-L20

The practical design is:

* **Attempt preview**
  hidden from the main workspace by default; bound to an internal attempt id; automatically cleared on retry/rollback/cancel.

* **Published preview**
  visible in the normal workspace preview surface; bound to suggestion id and context version; survives until accept, reject, supersede, or expiration.

This lets you reuse the same geometry/rendering code without letting hidden attempts pollute the live board or the visible preview surface.

### The self-review loop

A good Next Action self-review loop for geometry-heavy work should look like this:

```text
sense context
→ generate one or more candidate deltas
→ apply candidate to shadow/scratch state
→ observe the resulting geometry/structure
→ validate against rules + heuristics + intent
→ compare against current best
→ repair or regenerate if needed
→ stop when convergence, budget, or confidence threshold is reached
→ publish final preview or stay silent
```

That pattern fits both the session-runtime design and the self-refinement literature. KiSurf’s current session spec already says the core owns typed mutations, shadow-board truth, rollback, journal replay, validation, and final accept, and that live-board mutation during preview is forbidden. The research literature supports the same structure from a reasoning perspective: generate, critique, refine, and verify intermediate steps before finalizing. fileciteturn42file0L14-L29 fileciteturn42file0L31-L55 citeturn0academia1turn0academia0turn8academia0

### A recommended state model

The current branch has `Pending`, `Previewing`, `Accepted`, `Rejected`, and `Expired`. That is a solid base, but for a true background “brain” I recommend adding two explicit internal states and one external one:

* **Attempting**
  internal-only; the Agent is running hidden scratch attempts.

* **Superseded**
  explicit terminal state when a newer suggestion for the same interaction locus replaces an older one.

* **Cancelled**
  explicit internal/external state when the context changed mid-attempt and the attempt was aborted before publication.

Combined state flow:

```text
Idle
→ Attempting
→ Pending
→ Previewing
→ Accepted | Rejected | Expired | Superseded
```

The current code already gives strong support for version-bound expiration. Suggestions carry context version; the orchestrator expires active suggestions when document, selection, or view revisions no longer match; and the session runtime rejects stale accept by default, including explicit `selection_conflict` handling. fileciteturn39file0L77-L85 fileciteturn23file0L82-L96 fileciteturn42file0L43-L45 fileciteturn43file0L64-L87

### Latency budgets

For real-time usability, I recommend three budgets:

* **Hot path budget** for deterministic native reactions: tens of milliseconds
* **Warm path budget** for one or two internal geometric attempts: roughly a few hundred milliseconds
* **Cold path budget** for model-backed or more complex planning: sub-second, and only on stable pauses or semantically clear triggers

The exact numbers are a product decision, but the architectural principle is directly supported by the branch spec: do not call the model on every cursor motion; use native deterministic providers on high-frequency events; use model-backed suggestions only on coarse triggers or debounced stable pauses. fileciteturn6file0L80-L93

## Work-state patterns

### Placement

The current branch already treats placement as a first-class work state. Tool state includes `PlacingFootprint`, workspace contexts include `FootprintPlacement`, and KiSurf now generates semantic placement candidate anchors instead of making the model guess pixels. The footprint-placement anchor design deliberately exposes deterministic points such as `tool.placement.cursor`, `tool.placement.grid.east`, `tool.placement.grid.south`, and `tool.placement.grid.diagonal`, and the visual-read defaults can automatically highlight them so the model sees the same choices in both text and image channels. fileciteturn39file0L55-L75 fileciteturn34file0L37-L76 fileciteturn35file0L5-L18 fileciteturn35file0L30-L45

That is the right starting point, but it is not enough for final placement quality. KiCad’s own board semantics make it clear that legal and good placement depends on more than cursor-adjacent availability: footprints have courtyards and overlapping courtyards create DRC violations; rule areas can keep out footprints and other object classes; snapping and grid origins matter; selected ratsnest visibility can help interpret connectivity pressure; and dragging a footprint in KiCad reverts if the final dropped position violates DRC. citeturn5view0turn4view2turn5view1turn5view2turn4view0turn4view4

My recommendation is that the Agent’s placement loop should progress through four layers:

#### Intent understanding

The Agent should first infer **what kind of placement continuation** the engineer is doing. Examples:

* continuing placement of a just-selected footprint
* arranging a related cluster
* continuing a repeated row/column pattern
* adding a via, zone, keepout, guard-ring segment, or stitching pattern
* nudging a component after local rerouting pressure becomes obvious

The signals for that inference should come from selection, active tool, cursor neighborhood, recent activity, nearby anchors, visible nets, and panel state. KiSurf’s unified context snapshot is explicitly designed to carry exactly those signals. fileciteturn32file0L7-L23 fileciteturn32file0L56-L73 fileciteturn39file0L241-L262

#### Candidate generation

The Agent should ask tools for **candidate anchors and candidate transforms**, not just raw coordinates. Good placement tools should be able to produce:

* anchor-based targets
* orientation candidates
* symmetry/mirror candidates
* pattern-continuation candidates
* “pack near related net cluster” candidates
* “respect room/rule area” candidates

The current branch’s anchor work makes this direction explicit: placement anchors are “semantic starting points for preview work,” not claims of final legality or optimality. fileciteturn34file0L62-L76

#### Scratch placement and observation

For each promising candidate, the Agent should place the object into a **shadow/scratch board state** and then observe metrics such as:

* overlap and courtyard conflict
* keepout/rule-area violations
* distance to related components or nets
* ratsnest delta
* local density / routing pressure
* alignment with rows, symmetry, or mechanical edges
* orientation consistency

This is where the session-runtime architecture matters. The shadow board, typed journal, rollback, and render-preview path already establish the correct mechanism for tentative edits that must never mutate the live board until acceptance. fileciteturn42file0L14-L29 fileciteturn42file0L147-L185

#### Repair and publish

If the candidate is poor, the Agent should either:

* locally repair it, for example by snapping to an alternate anchor, rotating, or shifting by legal pitch/grid offsets, or
* abandon it and test another candidate.

Only after internal review passes should the Agent publish a **single native placement preview**. The preview should emphasize the moved/placed object, show any important overlays such as clearance or overlap warnings, and carry a concise rationale such as “closer to U3 decoupling cluster” or “continues the existing connector row.” The preview manager’s overlay model is already compatible with such annotations. fileciteturn24file0L33-L50

In short, for placement, the Agent should mostly reason in **anchors, transforms, and scored candidate placements**, not raw coordinates.

### Routing

Routing is where the architecture matters most.

KiCad’s official PCB editor documentation says the interactive router already supports push-and-shove, walkaround, highlight-collisions, differential-pair routing, length and skew tuning, live temporary segments from the routing start point to the cursor, and DRC-aware prevention of illegal routes in shove/walkaround mode. It also exposes clearance behavior, router modes, and differential-pair constraints such as gap, skew, via behavior, and uncoupled-length checks. citeturn3view0turn4view3turn11view4turn11view1

The KiSurf branch, meanwhile, already moves in the right direction by augmenting routing context with semantic anchors derived from tool state, adding `kisurf_preview_route_to_anchor`, and then relaxing the tool so `net`, `layer`, and `width` can be inferred from anchor metadata or routing tool state rather than being redundantly re-specified every time. That is a very strong signal that **anchor-oriented and intent-oriented routing interfaces are more stable than coordinate micromanagement**. fileciteturn36file0L17-L27 fileciteturn36file0L63-L123 fileciteturn30file0L17-L35 fileciteturn31file0L33-L45 fileciteturn31file0L91-L118

#### Routing abstraction comparison

My recommendation is a **layered routing control stack**, not one universal abstraction.

| Abstraction | What the Agent does | Strengths | Weaknesses | Long-term role |
|---|---|---|---|---|
| Raw coordinate or pixel operations | Chooses exact points/segments | Maximum freedom for unusual edits | Brittle, hard to validate semantically, easy to overfit visual accidents, contradicts KiSurf’s move away from “pixel guessing.” fileciteturn2file0L35-L42 fileciteturn34file0L7-L14 | Keep **below** the model-facing layer, inside planners and low-level tools |
| Anchor/reference operations | Chooses named start, target, breakout, or placement anchors | Stable, inspectable, works with structured context, easy to highlight visually, aligns with current KiSurf routing and placement anchor work. fileciteturn30file0L65-L76 fileciteturn36file0L63-L123 | Still too low-level for longer multi-segment or bus-level planning | Best **primary** model-facing primitive for local routing continuation |
| Candidate path selection | Selects among several planner-produced route candidates | Good balance of control and safety; easy to compare DRC, vias, layers, length, and future space use | Requires stronger candidate-generation infrastructure | Best **review-time** abstraction once candidate routing tools mature |
| Intent-driven routing | States “continue to that pad on same layer if possible,” “preserve pair coupling,” “prefer no via,” etc. | Closest to user intent, easier to scale to buses, diff pairs, fanout, and staged planning | Needs a planner and richer validators underneath | Best **high-level** abstraction for long-term evolution |
| Router-as-tool | Calls native router/planner as a bounded service | Reuses proven KiCad capabilities; native DRC and routing semantics are already present. citeturn4view3turn11view4 | If made too opaque, it displaces the Agent and becomes the real decision maker | Should be used heavily, but as a **subordinate proposal/validation tool**, not the policy owner |

#### The recommended routing stack

For KiSurf specifically, the best long-term stack is:

* **Intent layer** for “what kind of continuation should happen”
* **Anchor layer** for semantically meaningful endpoints and breakouts
* **Candidate-path layer** for one or more planner/router proposals
* **Router-as-tool layer** for actual geometry generation, shove/walkaround evaluation, via insertion, and legality checks
* **Coordinate layer** hidden inside the tool/runtime

That structure gives you the best of all worlds:

* the Agent is still the brain
* the router remains a tool
* suggestions are explainable
* latency stays manageable
* routing can scale from one short segment to more global continuation logic

#### What the Agent should review for routing

The Agent should not treat “DRC legal” as sufficient. It should review at least:

* connectivity correctness
* clearance and layer legality
* via count and via placement quality
* directionality and local posture relative to current route intent
* differential-pair and bus-spacing implications where relevant
* consistency with active-tool context and recent user behavior
* likely impact on future routability, not just the current segment

That is not speculative; KiCad already exposes router modes, active routing posture, temporary segment previews, custom-rule-driven constraints, differential pair gap/skew/uncoupled checks, and clearance-resolution tools. So the product already has a rich substrate for an Agent to ask not only “is this legal?” but also “what rule bound this choice?” and “what hidden future cost did this segment create?”. citeturn4view3turn11view1turn11view4

#### The routing self-review loop I recommend

For local continuation, the Next Action Agent should do this:

```text
read routing tool state + dynamic routing anchors
→ infer local routing intent
→ ask planner/router for N bounded candidates
→ apply each candidate to scratch state
→ query validation + connectivity + route metrics
→ optionally call a “future-space” heuristic tool
→ keep best candidate or repair with alternate anchor/layer/via policy
→ publish one final route preview
```

The hidden-attempt piece is crucial. KiCad’s own routing UX already uses temporary unfixed route segments while routing, and only saves fixed segments; KiSurf should generalize that idea into multiple hidden planner attempts before the user sees the final proposed preview. citeturn4view3

### Auto-filling and refilling

Auto-filling is simpler, but it should still be Agent-mediated.

The current branch already has a deterministic `AI_PANEL_TABLE_NEXT_ACTION_PROVIDER` that inspects focused semantic panel tables, detects a populated focused cell with multiple empty targets in the same column, and generates a reviewable `panel_fill_column_preview` suggestion. The implementation deliberately leaves preview and edit objects empty today and positions this as a preview-first suggestion rather than an automatic change. fileciteturn10file0L7-L10 fileciteturn10file0L70-L78 fileciteturn17file0L78-L120

That is exactly the right pattern for auto-fill. The difference from placement/routing is not “this work state doesn’t need the Agent.” The difference is that its self-review is mostly **schema and consistency review**, not geometric trial-and-error.

The recommended auto-fill loop is:

```text
detect focused structured context
→ infer likely fill candidates from nearby values, schema, project context, and recent edits
→ validate candidate against schema/enum/rule consistency
→ package as reviewable fill preview
→ expire immediately if focus, panel state, or context version changes
```

The unified context design already added panel-state records precisely so the same single context carrier can serve background next-action suggestions and chat, instead of inventing a separate panel-specific API. fileciteturn32file0L7-L23 fileciteturn32file0L185-L245

For auto-fill, I recommend three review layers:

* **schema validity**
  correct type, enum membership, required fields, row/column applicability

* **local consistency**
  consistent with neighboring cells, repeated table patterns, focused-row semantics

* **project consistency**
  compatible with net classes, board rules, component properties, or other existing design data

Because these suggestions are lightweight, the Agent can usually stay within one pass, unlike placement/routing where multiple hidden attempts are often justified.

## State management, stale handling, and coexistence with Chat Agent

### Version binding and stale behavior

Every Next Action suggestion should be bound to a **compound context version**, not just a board revision. The current branch already models `AI_CONTEXT_VERSION` as document, selection, and view revisions, and it already expires active suggestions when that version changes. It also records trigger activity sequence and attaches context kind/details metadata to suggestions. fileciteturn39file0L77-L85 fileciteturn40file0L19-L48 fileciteturn23file0L82-L96 fileciteturn7file0L7-L10

For Next Action, that should be extended conceptually into two scopes:

* **publication binding**
  the context version that makes a preview valid for user display

* **attempt binding**
  the stricter state snapshot under which a hidden attempt may continue running

A user moving the cursor slightly may not need to kill a longer-running table-fill attempt, but it should immediately kill a geometric placement or routing attempt that depends on cursor position and active tool state.

### Cancellation and supersession

I recommend these precise rules:

If the user changes tool, selection, focused panel cell, active route start, or placement cursor in a way that invalidates the suggestion locus, the active attempt should be **cancelled immediately**. If a newer attempt for the same locus produces a fresher preview, the old preview should become **superseded**, not merely expired. Expiration should be reserved for suggestions that became stale due to broader context drift or elapsed relevance. This distinction becomes useful in logs, metrics, and future tuning. It also fits naturally with KiSurf’s existing observability direction. fileciteturn8file0L7-L10 fileciteturn20file0L57-L93

### Accept, rollback, and auditability

The session-runtime spec already provides the right mental model for reliable reversible behavior: shadow-board truth during a session, journal truth for replay/audit, preview truth in KIGFX, accept as a core-owned promotion, stale accept rejected by default, and selection conflicts explicitly surfaced. The accept applier checks base hash, can reject insufficient validation before replay, opens one `BOARD_COMMIT`, replays the journal, and avoids half-mutating the board on failure. fileciteturn42file0L14-L29 fileciteturn43file0L22-L45

That leads to a strong recommendation:

**Every Next Action preview should be representable as a tiny constrained session or session-like micro-journal, even when the actual suggestion is simple.**

That gives you:

* replayability
* rollback
* structured observation
* validation provenance
* future training data
* a single accept boundary shared with the Chat Agent

### Relationship with Chat Agent

The README is explicit that Chat Agent and Next Action Agent should share editor context, preview, validation, and operation infrastructure while serving different product roles. The session-runtime spec further says only one active AI execution session is allowed per board tab in the MVP, and that selection conflicts and stale accept should fail closed rather than rebasing automatically. fileciteturn1file0L54-L77 fileciteturn42file0L43-L47

My recommendation is therefore:

* **Shared vocabulary, different budgets**
  Both agents should use the same core tool families, preview service, validation service, and accept path. Next Action gets stricter time budgets, narrower permissions, and smaller working memory.

* **Observation can be parallel; editing cannot**
  Both agents may observe concurrently, but only one may own the board-editing lease or preview surface for a given board tab at a time.

* **Next Action should degrade gracefully during an active Chat session**
  When Chat owns the editing session, Next Action should either pause or fall back to observation-only suggestions that do not open competing preview surfaces.

* **One preview surface, one owner**
  Avoid simultaneous preview overlays from both agents. The product should have a notion of preview ownership or surface partitioning.

This is less about safety policy than about product coherence. If two agents compete for the same board state and preview surface, the experience will feel broken even if each subsystem is technically correct.

## Recommended design principles for long-term evolution

The most stable long-term design for KiSurf is not “more tools,” but **better tool layering**.

The session-runtime spec already rejects a proliferation of bespoke model-facing composite PCB tools and instead favors session tools, bounded observation tools, typed atomic operations, and SDK/helpers that lower into inspectable journals. That is the correct long-term move for Next Action as well. fileciteturn42file0L57-L95

From that foundation, I recommend these principles.

### Keep the Agent in charge of publication

Candidate generators, routers, solvers, and validators may be sophisticated, but they should not publish user-visible suggestions on their own. They should return candidates and evidence; the Agent decides whether to keep trying, repair, drop, or publish. That is what keeps the Next Action Agent as the brain rather than letting the router or scorer silently take over. fileciteturn6file0L84-L93

### Prefer semantically stable interfaces over overly concrete ones

Anchors, intent descriptors, typed operations, and context-state records are more evolution-friendly than raw coordinate pokes or pixel-dependent controls. The branch already moves in this direction for routing and placement. fileciteturn30file0L17-L35 fileciteturn31file0L33-L45 fileciteturn34file0L7-L14

### Make every modifying path previewable, rollbackable, verifiable, and auditable

This should be a hard architectural invariant. KiSurf’s current runtime already encodes it: no direct live-board mutation during preview, typed atomic operations, checkpoints, rollback, preview services, validation, and accept replay. fileciteturn42file0L31-L55 fileciteturn43file0L22-L45

### Separate stable core tool contracts from experimental heuristics

Stable long-term interfaces should cover:

* context snapshot
* tool state
* anchors
* panels
* scratch edit application
* preview rendering
* validation
* rollback
* accept replay
* candidate provenance

Experimental tools should cover things like:

* future-routability heuristics
* learned placement scoring
* symmetry detectors
* bundle routing candidate generation
* bus escape planning
* domain-specific fill heuristics

That split lets KiSurf evolve aggressively while keeping the core agent/tool contract stable.

### Use hidden attempts for geometry, lightweight review for structured fills

Not all work states deserve the same deliberation cost. Placement and routing usually justify multi-attempt self-review because geometry can surprise you after actual tentative application. Auto-fill usually does not. The architecture should make that difference explicit while keeping the same outer loop. That is how you avoid three fragmented systems while still respecting the very different needs of three work states. citeturn0academia1turn8academia0

### Treat silence as a first-class outcome

A proactive agent that constantly speaks is a bad collaborator. The branch already emphasizes stale expiration, debouncing, and avoiding model calls on high-frequency events; the proactive-assistant literature makes the same point from a UX angle. Sometimes the best Next Action is **no suggestion yet**. fileciteturn6file0L80-L93 citeturn13academia0

In practical terms, the architecture I recommend for KiSurf is:

```text
One unified Next Action brain
+ shared native context / preview / validation / accept substrate
+ constrained scratch execution for hidden attempts
+ semantically stable tool contracts
+ work-state-specific sensing and validators
+ version-bound publish / accept / stale handling
```

That architecture best matches the code and specs already present in `codex/ai-native-session-runtime`, best fits KiCad’s native interaction model and router/DRC capabilities, and gives KiSurf the cleanest path from today’s preview-only foundations to a durable AI-native EDA editor where the Agent is truly the in-editor collaborator rather than a thin UI shell around disconnected algorithms. fileciteturn1file0L190-L196 fileciteturn9file0L18-L40 fileciteturn5file0L96-L124 citeturn3view0turn4view3turn11view1