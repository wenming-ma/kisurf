# KiSurf Next Action Implementation Strategy

## What the current branch already establishes

The current KiSurf branch is already aligned with the core product premise in your brief: KiSurf positions itself as an AI-native PCB editor built on KiCad, explicitly not as a thin plugin, an external MCP wrapper, or a chatbot pasted over a disconnected design model. In the repository README, the project states that AI should be a first-class collaborator inside the schematic and PCB editors, and that the Next Action Agent is a unified, LLM-mediated runtime whose loop is observe, decide, call tools in hidden shadow state, render and validate, then publish, retry, roll back, or abandon. The same README also says that placement, routing, and auto-fill/refill are work states inside one runtime rather than separate agents. citeturn44view0turn14view1turn31view0

In code, that foundation is real rather than aspirational. The `AI_NEXT_ACTION_RUNTIME` type already has explicit data structures for semantic events, observation packets, LLM decision turns, review turns, preview leases, accept ownership tokens, attempt records, and runtime steps. The runtime exposes `Update`, `BeginPreview`, `Accept`, `Reject`, `ExpireStale`, step history, and attempt history. Attempt records already carry hidden-session identifiers, hidden-step identifiers, base checkpoint identifiers, journal JSON, render outputs JSON, validation facts JSON, rollback JSON, and provenance JSON. citeturn18view0turn17view0

The scheduling layer is also present, but still minimal. The current scheduler suppresses raw pointer-motion style events such as `mouse.move`, `cursor.move`, and `pointer.move`, builds a semantic event from editor context when a trigger is meaningful, and debounces repeated events for the same semantic slot with a minimum interval of 500 ms. The context signature already includes document revision, selection/view revisions, tool-mode version, UI-focus version, and activity sequence, which is the right direction for stale detection and supersession. citeturn23view0turn17view0

The current tool layer is intentionally generic and still narrow. `AI_NEXT_ACTION_TOOL_REGISTRY` presently advertises a small catalog—`observation.read`, `candidate.generate`, `shadow.apply_candidate`, `render.hidden_attempt`, `validate.hidden_attempt`, `rollback.attempt`, and `publish.preview`—and its candidate generation currently fans out to three legacy/high-level providers: via-pattern, routing-segment, and panel-table next-action providers. That means the branch already has the *shape* of a tool-mediated runtime, but not yet the production-grade tool surface you want for placement, routing, and structured fill/refill. citeturn47view0turn22view0turn22view1

The read-review-publish loop is also in place. `Update()` builds an observation packet, sends a `NextActionDecision` request to the provider, generates candidates, executes up to two attempts, sends a `NextActionReview` request, and only publishes if review returns a publish decision. The tests verify that publish happens only after decision and review turns, that rollback-retry records a checkpoint rollback, that hidden-attempt metadata is stored, and that published suggestions expire when context revisions change. citeturn20view1turn23view1turn28view2turn30view0turn30view1turn29view6

The most important implementation gap is that the current observation and validation payloads are still MVP placeholders. The observation packet currently serializes only a minimal fact set—slot id, reason, dynamic context kind, tool-state kind, and whether a visual snapshot has pixels—while the hidden-attempt validation result is still a stub with `drc_error_count: 0`, empty clearance/connectivity arrays, and `status: "not_blocked"`. This is good enough to prove the outer runtime, but not good enough to deliver production-quality placement, routing, or fill suggestions. citeturn20view4turn22view1

One additional issue is worth calling out explicitly because it affects safety: the runtime already embeds preview-lease and accept-token provenance into suggestions, and `CanAccept()` checks that the token is internally consistent and that validation has no blocking issue. But `Accept()` itself takes only a suggestion id and edit session, then applies edit objects if `CanAccept()` passes; it does **not** independently consume the *current* editor context or re-resolve lease/token freshness at accept time. That means the current design depends on correct external stale-expiration sequencing. I read this as an inference from the code, not an explicit repository claim, and I would tighten it before shipping. citeturn26view0turn26view1turn24view0turn20view3

## Recommended architecture

The right production architecture is to keep **one** `AI_NEXT_ACTION_RUNTIME` as the orchestration brain, but make its tool surface more explicit and layered. The repository’s own research notes already point in this direction: one unified runtime above the shared session substrate, with typed atomic operations, shadow-board execution, preview rendering, validation, checkpoint/rollback, and accept replay shared with the Chat Agent, but governed by a stricter ambient policy for Next Action. citeturn31view0turn34view2turn33view1

I recommend preserving the three-layer shape from your brief, with one refinement: the “script” layer should not be an unrestricted Python execution surface. It should be a **bounded hidden-attempt program** that compiles to typed atomic operations and integrated-tool invocations, and runs under session/runtime control with strict budgets, rollback boundaries, and publication gates. That keeps the LLM as the semantic decision-maker while preventing the script layer from becoming an unbounded second runtime. This recommendation matches both the current KiSurf session direction and the existing runtime’s explicit separation between decision, hidden mutation, validation, and publish. citeturn32view0turn47view0turn20view5turn20view6

A good long-term split looks like this:

| Layer | What it should expose | Who owns it | What it must never do |
|---|---|---|---|
| Atomic operations | Typed queries, typed mutations, checkpoints, rollback, render, validate, journal readback, accept replay | Core runtime and KiCad-integrated editor code | Never publish a suggestion or decide “this is the best next action” |
| Integrated tools | Placement anchor generators, router-backed candidate generation, route-to-anchor, parallel-route helpers, table/panel schema helpers, scoring summaries | Native KiCad/KiSurf helpers, built on atomic ops | Never publish directly; never become an autonomous policy layer |
| Hidden-attempt program | Small loops, branching, retries, variable binding, candidate iteration, local repair, rollback-and-retry | Next Action runtime profile on top of the shared session substrate | Never bypass render/validation/review/publish gates; never mutate live board |

This is also the correct way to evolve from the current provider-style candidate generation. Today, `candidate.generate` still hides concrete providers under one generic name. Production KiSurf should keep a *small* stable core, but expose a richer namespace of integrated tools so the LLM can combine them without you hardcoding every composite workflow. The stable model-facing vocabulary should be semantically narrow and compositionally rich: query, generate, apply-in-shadow, validate, render, rollback, publish, accept. The evolving helpers can sit behind that boundary. citeturn47view0turn34view1turn33view3

The architecture should therefore become **work-state aware, not work-state fragmented**. Concretely:

- Common tools should cover context query, selection query, viewport query, board revision query, layer/net/rule lookup, checkpoint, rollback, preview render, structured validation, preview publish, accept replay, and attempt-journal introspection.  
- Placement-specific tools should cover anchors, footprint transforms, overlap/courtyard/keepout/ratsnest scoring, and local repair.  
- Routing-specific tools should cover routing intent compilation, anchor resolution, corridor generation, candidate-route generation, route-parallel helpers, router parameter changes, and low-level segment/via ops as a fallback.  
- Fill/refill-specific tools should cover focused widget context, schema lookup, legal-value enumeration, patch validation, and typed patch application.  

That keeps the LLM as the chooser of *what to try* and *whether it is worth showing*, while allowing KiCad-native algorithms to do the heavy geometric or typed work. citeturn33view3turn33view4turn34view1

## Observation packets and hidden attempts

The current observation packet is too small for the quality bar you want. At present it includes the semantic slot id, reason, dynamic-context kind, tool-state kind, and minimal visual availability facts. That is enough to let the LLM decide “attempt or wait,” but not enough to drive reliable placement, routing, or structured UI fills. The long-term packet should still be versioned and compact, but it must include more of the editor state that KiSurf already treats as core context: selection, recent activity, tool mode, viewport, board revision, and structured work-state context. citeturn20view4turn18view0

For MVP, I recommend a **two-part observation packet**.

The first part should be common to all work states:

- `context_signature`: document revision, selection revision, view revision, tool-mode version, UI-focus version, activity sequence, semantic slot id, and base board hash.  
- `editor_state`: editor kind, active tool, current selection summary, cursor position if meaningful, viewport bounds, visible layers, and recent semantic activity summary.  
- `design_state`: current net and layer summaries, relevant design-rule summary, connectivity snapshot version, and whether hidden preview is already armed.  
- `visual_state`: a compressed board-canvas snapshot handle plus structured visible-object summaries near the focus region.  

The second part should be **state-specific**:

- Placement: target footprint(s), anchor footprint(s), pad clusters, keepouts, board edge distances, local congestion, local ratsnest summary, symmetry/peer-part hints.  
- Routing: route start anchor, likely target anchor(s), active net or diff-pair/bus group, allowed/preferred layers, via policy, visible obstructions, local guide corridor if any, router mode.  
- Fill/refill: focused widget id, panel/table surface type, selected rows/columns/cells, relevant schema, object ids, current values, validation hints, and recent typed edits.  

This packet is intentionally “minimum useful,” not maximal. The runtime should send structured summaries first, not huge raw board dumps. The LLM only needs enough to choose the next attempt intelligently. More detail should be fetched with tools. citeturn34view2turn33view3turn34view0turn34view1

On hidden attempts, I do **not** recommend inventing a separate lightweight runtime unless performance data later forces it. The repository’s research notes, runtime types, and tests all point the other way: KiSurf already has the right substrate in the shared session runtime—session lifecycle, typed atomic ops, shadow-board execution, checkpoints, rollback, preview bridge, validation, journaling, and accept replay—and tests already expect hidden-session ids, hidden-step ids, base checkpoints, journals, and provenance to exist for next-action attempts. Reusing that substrate with a stricter policy profile is the fastest path to production correctness. citeturn32view0turn34view2turn30view0turn30view1

The constrained hidden-attempt session should therefore behave like this:

1. Open or reuse an ephemeral `nextaction` shadow session scoped to one semantic slot and one work state.  
2. Take a base checkpoint before the first mutation.  
3. Run one bounded hidden-attempt program that can call atomic ops and integrated tools.  
4. Render internal preview artifacts into a **non-user-visible** preview tier.  
5. Collect validation facts and summary diagnostics.  
6. Either publish a single user-visible preview, or roll back and retry, or abandon.  
7. On publish, bind preview lease and accept token to the resulting suggestion.  
8. On accept, replay the reviewed journal into one live-board transaction.  

That model aligns with KiSurf’s current preview-first direction and with the runtime’s existing provenance structures. citeturn34view3turn26view0turn26view1turn22view1

For ownership and conflict control, I recommend three rules. First, a Chat Agent edit session should always outrank Next Action; if an explicit chat session holds mutation ownership, Next Action can still observe but must not open a hidden mutation session against the same board region. Second, only one published Next Action preview should be active per semantic slot/work-state family at a time. Third, a hidden attempt may reuse a session if the same slot remains valid, but any change in context signature should force either rollback to base checkpoint or full attempt abandonment. The current runtime already has the concept of semantic slot ids, active suggestion lookup, and context-based expiration; extend that instead of adding a second ownership model. citeturn23view0turn23view2turn24view2

The current attempt limit is two candidates per episode. That is a sensible proving-ground cap, but I would make it state-specific in production: placement up to three attempts, routing up to four hidden attempts in narrow local contexts, fill/refill normally one attempt and at most two. The budget should be enforced by runtime policy, not left to the model. citeturn23view1

## MVP vertical slices

### Placement

Placement should be the first production slice because it is high-value, visually legible, and easier to validate incrementally than routing. KiSurf’s own research notes recommend anchor-based placement rather than raw coordinate guessing, and KiCad’s PCB editor concepts support that framing well: footprints, pads, local geometry, keepouts, board edges, and transient previews already exist as native editor concepts. The KiCad board editor also supports property editing through dialogs and the docked Properties Manager, and board objects have well-defined layer, object, and geometry models. citeturn33view3turn37search0turn36search12turn39search7turn43search1

The MVP placement toolset should be:

| Category | Tool |
|---|---|
| Query | `pcb.query_placement_context`, `pcb.query_selected_footprints`, `pcb.query_local_rules`, `pcb.query_local_obstacles` |
| Candidate generation | `placement.generate_anchor_candidates`, `placement.generate_region_candidates`, `placement.generate_symmetry_candidates` |
| Mutation | `placement.apply_transform_shadow` |
| Evaluation | `placement.eval_overlap`, `placement.eval_courtyard_keepout`, `placement.eval_board_edge_margin`, `placement.eval_ratsnest_delta`, `placement.eval_orientation_quality` |
| Repair | `placement.local_translate_search`, `placement.try_rotation_variants`, `placement.try_mirror_or_pair_variant` |
| Publish path | `render.hidden_attempt`, `validate.hidden_attempt`, `publish.preview` |

The first concrete user-visible placement workflows should be narrow and composable:

- continue a partially placed decoupler or stitching-via pattern,  
- suggest a new location for one selected footprint relative to nearby pads or board edge,  
- propose a symmetry-preserving nudge for paired parts,  
- suggest a connector edge-clearance fix,  
- continue similar placement for repeated local structures.  

These are exactly the kinds of flows where “semantic anchor + local repair + preview” beats free-form coordinate generation. citeturn33view3turn44view0

Before preview, the review packet back to the LLM should include at least: overlap/collision summary, courtyard/keepout violations, board-edge margin, orientation, nearest-reference distances, ratsnest delta, whether the candidate stayed inside the visible work region, and whether local repair had to distort the original intent. If any hard constraint fails, the LLM should only be allowed to publish if the preview is explicitly explanatory and marked as a warning suggestion rather than a ready-to-accept placement. citeturn33view3turn22view1

### Routing

Routing should reuse KiCad’s interactive router internals rather than forcing the LLM into segment-by-segment generation. KiCad’s PCB editor documentation explicitly describes an interactive router with push-and-shove, walkaround, collision-highlighting, differential-pair routing, and length/skew tuning. In Doxygen, `PNS::ROUTER` exposes routing methods and state that map naturally to a tool boundary: `Settings()`, `GetNearestRatnestAnchor()`, `GetCurrentNets()`, `SwitchLayer()`, `ToggleViaPlacement()`, `FixRoute()`, `Finish()`, `StopRouting()`, and routing-in-progress state. That is strong evidence that KiSurf should wrap router-native abstractions instead of flattening routing into raw geometry tokens. citeturn38search0turn41view2turn41view3turn41view4turn41view5turn41view6turn42view0turn42view3turn42view4

The model-facing abstraction should therefore be **intent first**, with anchors and candidate paths as the main middle layer:

- The LLM decides route intent: continue current route, finish to anchor, fanout, dodge/repair, continue diff pair, continue bus, or maintain corridor/style.  
- Native tools resolve candidate start/end anchors, router settings, permitted layers, via allowance, and optional corridor hints.  
- The router generates one or more hidden candidates.  
- The runtime renders and validates them.  
- The LLM reviews whether one candidate is worth previewing.  

This is the right boundary because `PNS::ROUTER` already understands current nets, anchors, layers, router state, and size settings; KiSurf should make those concepts explicit tools instead of exposing low-level segment authoring as the default surface. citeturn33view4turn34view0turn42view0turn41view4turn41view5

The MVP routing tools should be:

| Category | Tool |
|---|---|
| Query | `routing.query_context`, `routing.resolve_start_anchor`, `routing.resolve_target_anchors`, `routing.query_router_mode` |
| Candidate generation | `routing.generate_candidates`, `routing.route_to_anchor`, `routing.route_parallel`, `routing.finish_to_nearest_anchor` |
| Router control | `routing.set_layers`, `routing.set_via_policy`, `routing.set_style_goal`, `routing.try_alternate_corridor` |
| Fallback expert ops | `routing.add_segment_shadow`, `routing.add_via_shadow`, `routing.undo_last_segment_shadow` |
| Evaluation | `routing.eval_connectivity`, `routing.eval_clearance`, `routing.eval_via_count`, `routing.eval_length_detour`, `routing.eval_parallelism_or_coupling`, `routing.eval_future_congestion` |

The expert/segment-level tools should exist, but only as a fallback layer for debugging or edge cases. The default production path should be anchor/corridor/candidate-route based. citeturn33view4turn34view0

Review facts for routing must go beyond binary DRC. At minimum, the LLM should see: whether connectivity was achieved, DRC/clearance result summary, via count, layer transitions, detour cost, whether the route finished to the expected anchor, diff-pair or bus-spacing quality where relevant, and whether the route is likely to block nearby unfinished routes. That is the minimum set needed to let the LLM decide whether a candidate is worth interrupting the user with. citeturn34view0turn22view1

### Auto-fill and refill

For fill/refill, start with the surfaces already clearly supported by the KiCad board editor: **Properties Manager** edits for selected objects, **table and table-cell editing**, and tightly scoped bulk property surfaces such as track/via properties. KiCad’s official PCB editor docs say that the docked Properties Manager can edit selected object properties, including mixed multi-selection shared properties; table cells and whole tables can be edited in dialogs and in the Properties Manager; multiple table cells can be selected; and selected tracks and vias can also be modified through the Properties Manager. Those are excellent first targets because they are typed, inspectable, and already native UI surfaces. citeturn37search0

The key design rule here is the one in your brief: schema providers and validators are tools, not decision layers. KiSurf’s own research notes are directionally aligned: structured completions should rely heavily on schema and rule providers because the target space is typed and small, but the final decision to fill one cell, a row, a column, a selected patch, or nothing at all should remain in the LLM-controlled loop. citeturn34view0turn34view1

The MVP fill/refill toolset should therefore be:

| Category | Tool |
|---|---|
| Query | `ui.query_focus_surface`, `ui.query_selected_cells_or_fields`, `ui.query_schema`, `ui.query_current_values`, `ui.query_related_object_context` |
| Candidate generation | `fill.enumerate_legal_values`, `fill.propose_patch`, `fill.propose_copydown`, `fill.propose_related_field_bundle` |
| Mutation | `fill.apply_patch_shadow`, `fill.apply_value_shadow` |
| Validation | `fill.validate_type`, `fill.validate_enum_range`, `fill.validate_cross_field_consistency`, `fill.validate_project_convention` |
| Publish | `fill.render_inline_preview`, `publish.preview` |

The first user-facing slices should be:

- table cell completions and copy-down/refill for selected regions,  
- multi-field property suggestions for selected PCB objects,  
- track/via parameter copy-down or inferred defaults where the schema is explicit,  
- lightweight bundles such as “fill these three related fields.”  

This is more valuable than starting with broad natural-language panel automation, because it gives you strong validation and debuggability early. citeturn34view1turn37search0

For the model’s output contract, avoid free-form text patches. Use a typed patch format such as:

- `set_value` for one field/cell,  
- `set_many` for multiple named fields on one object,  
- `grid_patch` for selected table coordinates,  
- `copy_down` or `copy_across` for simple fill patterns,  
- `bundle_preview` for related-field suggestions that are previewed as one accept unit.  

That maps cleanly onto auditable typed-op history, and it keeps accept/reject behavior consistent with placement and routing. citeturn34view1turn31view0

## Scheduling, preview, and accept interaction

The scheduling policy should become more selective than the current branch, but it should extend the same semantic-event model rather than replacing it. Today the runtime already suppresses raw pointer moves and debounces repeated semantic slots. That is the right starting principle: raw events should almost never call the LLM directly. Semantic episodes should open only after event coalescing and local cheap checks. citeturn23view0turn29view3

I recommend these event rules:

- **Never trigger directly from** raw mouse movement, panning, zooming, transient hover changes, or intermediate drag motion.  
- **Placement episodes** should open after selection change, move/drag settle, pattern continuation pause, or a local legality/conflict change after movement.  
- **Routing episodes** should open only when routing is active or just paused near a plausible anchor/corridor decision point, or when a prior routing attempt was abandoned locally.  
- **Fill/refill episodes** should open when typing pauses in a structured field, when a table selection stabilizes, or when focus moves to a typed property surface with strong defaults.  

That is consistent with the current scheduler’s anti-noise posture and with the repository’s own recommendation that Chat and Next Action differ mainly in policy, latency budget, and allowed interruption cost. citeturn23view0turn34view2

My recommended idle/stability thresholds for MVP are:

- placement: roughly 250–400 ms after relevant motion/selection settles,  
- routing: roughly 120–200 ms when the cursor pauses during an active local routing context,  
- fill/refill: roughly 150–250 ms after typing/focus stabilizes.  

These are recommendations, not repository facts, but they follow from the asymmetry already present in KiSurf’s research notes: route previews are visually expensive and should be conservative; fill/refill is cheaper and can be more aggressive; placement sits in between. citeturn33view6turn34view3turn34view1

Preview behavior should be strict:

- one published Next Action preview at a time per work-state family,  
- internal attempt previews stay invisible to the user,  
- only the final reviewed preview is shown by default,  
- previews expire on context drift,  
- newer previews supersede older siblings in the same semantic stream,  
- ignored previews should quietly age out without punitive suppression unless the user explicitly rejects them.  

That matches both the repository’s published design and the current runtime’s status/expiration model. citeturn34view3turn24view2turn21view7

The accept model should stay preview-first and lightweight, but it needs one hardening step. A published suggestion should carry provenance, lease, accept token, and context signature, exactly as the current runtime does. But production `Accept()` should also require the *current* editor context to be passed in and re-validated against the lease/token before replaying the journal into the live board. In other words, move stale-safety from “best effort via external expiration” to “guaranteed at accept time.” That is the single highest-leverage correctness fix I would make before broad rollout. This is my recommendation based on the current implementation behavior. citeturn26view0turn26view1turn24view0turn20view3

The agent should stay silent when a suggestion would be semantically weak, visually disruptive, too costly to inspect, blocked by missing context, or likely to be stale before acceptance. Silence is especially important for routing. If the runtime cannot produce a candidate that is legal enough, local enough, and inspectable enough, abandonment is better than noisy low-quality previews. citeturn34view2turn34view3

## Testing, evaluation, and unresolved decisions

The existing tests already prove several critical invariants: raw mouse moves are suppressed; identical semantic slots are debounced; publish occurs only after decision and review turns; rollback-retry records checkpoint rollback; hidden-attempt metadata is stored; and published suggestions expire when the context changes. Those are exactly the right first invariants, and they should become the seed of a much broader replay-and-eval harness. citeturn29view3turn30view0turn30view1turn29view6

I recommend a **single replay artifact** per Next Action episode with these sections:

- semantic event and context signature,  
- observation packet,  
- tool catalog version,  
- every tool call and result,  
- hidden-attempt journal and checkpoints,  
- render outputs and validation facts,  
- review decision,  
- published preview metadata,  
- accept/reject/ignore/expire outcome,  
- final live-board commit replay summary if accepted.  

That format maps directly to the runtime and attempt structures already present in the branch. citeturn17view0turn20view4turn28view2

For CI, build three families of fixtures:

- **synthetic placement boards** with decouplers, connectors, paired parts, board-edge constraints, and keepouts,  
- **synthetic routing boards** with local escape, finish-to-anchor, diff-pair, and congestion cases,  
- **synthetic fill/refill surfaces** with property grids, tables, mixed valid/invalid schemas, and repeated field bundles.  

These should include both deterministic golden expectations and fuzzier ranking/eval expectations. The deterministic side should assert safety and replay correctness; the quality side should measure acceptability. citeturn44view0turn37search0turn38search0

The minimum hard safety tests should include:

- hidden attempts never mutate the live board,  
- rollback restores the hidden base checkpoint completely,  
- stale suggestions cannot be accepted after context drift,  
- superseded previews lose accept authority,  
- Chat Agent ownership blocks conflicting Next Action mutation attempts,  
- accept replay is one coherent undoable transaction,  
- preview provenance always points back to the attempt journal that produced it.  

The current branch already tests several of these pieces indirectly; the production plan is to make them explicit and exhaustive. citeturn30view1turn29view6turn34view3

For quality metrics, use state-specific evaluation rather than one blended score:

- **Placement:** hard-constraint pass rate, ratsnest improvement, conflict-free preview rate, accept rate, post-accept undo rate, and average local repair count.  
- **Routing:** connectivity completion rate, DRC-clean rate, via count relative to baseline, detour penalty, accept rate, and post-accept reroute rate.  
- **Fill/refill:** type-valid rate, cross-field-valid rate, exact-match or user-edit distance after acceptance, acceptance rate, and reject-repeat suppression success.  

Those metrics are more meaningful than generic “model confidence,” because they align with what the user actually experiences in the editor. citeturn33view3turn34view0turn34view1

The main unresolved decisions are not conceptual anymore; they are engineering choices about boundaries and budgets.

The first unresolved decision is whether to keep `candidate.generate` as one generic tool or split it into named integrated tools now. My recommendation is to split now. The current generic catalog was perfect for proving the outer loop, but it will become a debugging bottleneck once placement, routing, and fill/refill each need richer observations and review facts. citeturn47view0turn22view0

The second unresolved decision is how far to let the hidden-attempt program go. My recommendation is: allow short loops, variable binding, checkpoint/rollback, and bounded retries, but ban unbounded recursion, unrestricted Python side effects, network access, or any path that can bypass typed-op journaling. That keeps composition power high without turning Next Action into a second free-form automation engine. citeturn32view0turn34view1

The third unresolved decision is accept-time safety. As noted earlier, I would treat this as the largest production risk in the current code path: leases and tokens exist, but accept-time freshness enforcement should move into the core `Accept()` path itself rather than relying on external expiration to have run first. That is the one place where I would change the implementation contract before widening the tool surface. citeturn26view1turn24view0turn20view3

Overall, the implementation strategy is clear: keep the unified LLM-mediated runtime, deepen the tool surface instead of hardcoding composite flows, reuse the shared hidden session substrate under a stricter Next Action profile, expand observation packets to “minimum useful context,” ship placement first, routing second, fill/refill third, and make preview leasing plus accept-time context validation the non-negotiable safety boundary. That path is the closest fit to both the code already landed in `wenming-ma/kisurf` and to KiCad’s native editor/runtime structure. citeturn44view0turn32view0turn47view0turn40search10turn40search6turn38search0