# KiSurf LLM Mediated Next Action Runtime

## What the current KiSurf branch already establishes

The public `codex/ai-native-session-runtime` branch already points in the right architectural direction for an LLM-mediated Next Action system. Its README states that KiSurf is not meant to be “a chatbot bolted onto an EDA tool,” but an AI-native editor built on KiCad, with two workflows that **share** editor context, preview, validation, and operation infrastructure while serving different product roles. For Next Action specifically, the branch already frames it as an ambient workflow that watches live editor state and proposes the next useful action in workspace-native preview form, with suggestions tied to editor revision, selection, tool state, and viewport so stale suggestions can expire. The same branch also says the current phase already includes a Python-first execution runtime with typed atomic operations, a semantic shadow board, an operation journal, checkpoints, rollback, native preview rendering, validation, accept replay, activity logs, and preview-only Next Action plumbing. It further notes active-routing route-to-anchor previews, placement candidate anchors around the cursor, visual reads with automatic anchor highlighting, anchor-focus operation previews, and stale accept rejection by base hash. citeturn6view0turn6view1turn6view2turn6view3turn6view4

That baseline matters because it means the missing piece is **not** a separate “placement engine,” “routing engine,” or “refill engine” that bypasses the model. The missing piece is a stronger **runtime contract** around the loop the repository already implies: observe live state, choose whether to act, run one or more hidden attempts against shadow state, render and validate those attempts, let the LLM review the results, and only then publish a user-visible preview. This is much closer to a typed ReAct loop with explicit self-correction than to a single-shot autocomplete. ReAct’s core claim is that reasoning and acting work better when interleaved, and Reflexion-like loops show why explicit feedback-driven review improves multi-step behavior. In KiSurf, the difference is that the “environment” is a live KiCad editor with geometry, DRC, connectivity, and preview surfaces rather than a text-only environment. citeturn2search0turn2search5turn2search7turn2search8turn6view2turn6view3

Because the current branch is still developer-preview and explicitly says production-level autonomous placement and routing are not finished, the most useful design move now is to turn the runtime into a **strict protocol**. The LLM should stay the semantic decision-maker, while deterministic components such as router, validator, renderer, candidate generator, and shadow mutation engine remain tools that the model invokes and reviews rather than parallel decision systems that can publish on their own. That division is also consistent with modern agent guidance: tool definitions and structured outputs should carry real engineering weight, because the reliability of the loop depends on typed calls and typed results, not on free-form natural language control. citeturn6view3turn1search0turn1search1turn1search5

## Recommended runtime model

The cleanest long-term architecture is a **single Next Action runtime** with one scheduler, one state machine, one step contract, and one shadow-execution substrate. Placement, routing, and auto-filling do not need separate agents. They need different **observation adapters**, different **tool bundles**, and different **review criteria** wired into the same loop. That keeps the “brain” unified while still allowing state-specific specialization. The repository’s own taxonomy already treats placement, routing, and auto-filling/refilling as categories inside one Next Action workflow, and its runtime already shares context, preview, validation, and operation infrastructure with the Chat workflow. citeturn6view0turn6view3

The loop I recommend is:

```text
SemanticEvent
-> ObservationPacket
-> LLM Decide
-> Tool Calls
-> Hidden Attempt on Shadow State
-> Render + Validate + Summarize
-> LLM Review
-> Retry / Rollback / Publish / Abandon
-> User Accept or passive Expire
```

This is deliberately **not** “LLM thinks once, tool executes once, preview appears.” It is also deliberately **not** “native heuristics produce candidates and the LLM just rubber-stamps the best score.” The LLM must decide whether a suggestion is warranted, which tool family to use, whether a result fits the current user intent, whether another attempt is worthwhile, and whether anything should be shown at all. That keeps the semantics centralized in the model while still letting the underlying runtime do hard geometric work efficiently. This aligns with ReAct-style interleaving, with Anthropic’s guidance that agents should rely on explicit tools, and with the repository’s own direction away from bespoke model-facing edit tools toward `kisurf_run_cell` plus session control tools, typed atomic operations, and preview-first workflows. citeturn2search0turn1search0turn1search4turn6view2turn6view4

The runtime should therefore treat every semantic cycle as a **RuntimeStep** with three parts:

```json
{
  "step_id": "uuid",
  "suggestion_stream_id": "uuid",
  "phase": "observed | reasoning | attempting | reviewing | published | abandoned",
  "context_version": {
    "board_base_hash": "sha256",
    "selection_version": 143,
    "tool_mode_version": 28,
    "viewport_version": 77,
    "ui_focus_version": 19,
    "activity_seq": 9012
  },
  "observation_packet_id": "uuid",
  "llm_decision_id": "uuid",
  "attempt_ids": ["uuid"],
  "published_preview_id": "uuid|null",
  "budget": {
    "wall_clock_ms": 700,
    "attempt_limit": 2,
    "token_budget_class": "fast-next-action"
  }
}
```

The most important design choice is that a `RuntimeStep` is **version-bound** and **auditable**. It should always know which observation it was based on, which attempt records it created, whether it published, and which mutable state version it is allowed to touch. This is how you keep stale suggestions out of the editor and how you make replay, debugging, and evals possible. The repository’s current use of shadow-board state, journals, checkpoints, observability logs, and base-hash rejection already supports that direction. citeturn6view2turn6view3

## Step contract and typed decision schemas

The minimum implementable protocol should revolve around five typed objects: `ObservationPacket`, `RuntimeStep`, `AttemptRecord`, `ReviewDecision`, and `PublishDecision`. The goal is not to mirror wire format perfectly today; it is to define a stable semantic contract so future tools can evolve without rewriting the runtime every time. OpenAI’s structured outputs and function-calling guidance are especially relevant here: once a system has multiple tool calls and multiple review turns, typed JSON schemas stop being “nice to have” and become the mechanism that prevents free-text side effects, invalid enums, and hidden control paths. citeturn1search1turn1search5

A good `ObservationPacket` should be **multimodal but layered**:

```json
{
  "observation_packet_id": "uuid",
  "kind": "placement | routing | autofill | unknown",
  "context_version": {
    "board_base_hash": "sha256",
    "selection_version": 143,
    "tool_mode_version": 28,
    "viewport_version": 77,
    "ui_focus_version": 19,
    "activity_seq": 9012
  },
  "user_activity": {
    "recent_events": ["mouse_pause", "selection_changed", "route_started"],
    "idle_ms": 220,
    "semantic_event": "cursor_stabilized_near_route_anchor"
  },
  "editor_state": {
    "editor": "pcbnew",
    "tool_mode": "interactive_route",
    "selection": ["pad:U5.3", "trackseg:..."],
    "focused_panel": "canvas",
    "active_layer": "F.Cu"
  },
  "viewport": {
    "world_bbox": "...",
    "zoom": 14.2,
    "cursor_world": [123400000, 88200000]
  },
  "visual_inputs": {
    "full_view": "frame_ref",
    "focus_crop": "frame_ref",
    "overlay_frame": "frame_ref",
    "diff_frame": null
  },
  "structured_facts": {
    "anchors": [...],
    "candidate_entities": [...],
    "drc_summary": {...},
    "connectivity_summary": {...},
    "overlap_summary": {...},
    "ratsnest_delta_hint": null,
    "table_schema": null
  }
}
```

The observation should not dump raw board state indiscriminately. It should provide a **semantic slice** of the live workspace: what the user is doing, what is selected, what tool mode is active, where the viewport is, what recent actions happened, and a compact bundle of geometry and validation facts relevant to that state. The repository already exposes activity timeline data, workspace view, visual frame metadata and pixels when available, and model-facing context/session/render/validation tools. It also already automatically enriches routing and placement visual reads with anchors and render directives, which is exactly the right direction: much of the observation shaping should happen **before** the LLM sees the packet. citeturn6view2turn6view3

I recommend that the LLM decision schema remain intentionally narrow. The model should not be allowed to “speak prose that the runtime interprets.” It should emit one of a few allowed semantic actions:

```json
{
  "decision_kind": "wait | gather | attempt | abandon",
  "opportunity_type": "placement | routing | autofill | none",
  "reason_code": "insufficient_context | likely_helpful | low_confidence | user_busy | stale_context",
  "requested_read_tools": [],
  "requested_attempt_plan": {
    "tool_family": "placement | routing | property_fill",
    "checkpoint_policy": "from_base | from_last_good",
    "expected_review_axes": ["drc", "overlap", "intent_fit", "visual_fit"]
  }
}
```

The same discipline should apply after each hidden execution. A `ReviewDecision` should say whether the attempt is acceptable, whether rollback is needed, and whether another attempt is worth the latency budget:

```json
{
  "decision_kind": "retry | rollback_retry | publish | abandon",
  "reason_code": "collision | keepout_violation | poor_path | stale_context | acceptable | schema_conflict",
  "attempt_id": "uuid",
  "publish_candidate_attempt_id": "uuid|null",
  "next_attempt_guidance": {
    "adjust_anchor_bias": "closer_to_group | fewer_vias | shorter_path | preserve_orientation",
    "max_extra_attempts": 1
  }
}
```

Finally, only a dedicated `PublishDecision` should be able to cross the boundary from hidden attempt to user-visible preview:

```json
{
  "publish": true,
  "attempt_id": "uuid",
  "preview_mode": "overlay | ghost_route | ghost_fill | marker_only",
  "expires_on_context_change": true,
  "accept_requirements": {
    "board_base_hash": "sha256",
    "preview_lease_id": "uuid",
    "owner": "next_action"
  }
}
```

This separation is important. Deterministic algorithm calls, routing tools, validators, renderers, and candidate generators may do a lot of work, but **none of them may publish**. Publication is a runtime transition that only occurs after an LLM review emits an allowed `publish` decision under a valid context version. That is the simplest way to preserve the user’s core expectation that the model is the semantic author of every Next Action suggestion, while still using industrial-strength geometry and validation tools under the hood. citeturn1search0turn1search1turn1search5turn2search0turn6view2turn6view3

## Scheduler and cancellation policy

The scheduler should not call the LLM on every raw UI event. Instead, it should transform high-frequency editor events into lower-frequency **semantic events** that are worth model attention. The repository already records user commands, selection, movement, and mouse clicks with coordinate and modifier detail; that is the right raw substrate. The scheduler’s job is to aggregate those streams into editor-intent signals such as “cursor stabilized near a placement anchor,” “active route appears stalled near an obstacle,” “table cell edit paused with inferable schema,” or “selection changed into a likely next-step context.” citeturn6view2turn6view3

A practical scheduler pipeline looks like this:

```text
Raw UI events
-> Debounce + coalesce
-> SemanticEvent detector
-> Priority + freshness gating
-> ObservationPacket builder
-> LLM step dispatch
```

The most useful semantic-event classes are these:

- **Placement opportunity events**, such as cursor pause near candidate anchors, selection of a just-placed or logically related component, footprint drag pause, or ratsnest-heavy local rearrangement.
- **Routing opportunity events**, such as active route mode with a valid start anchor, cursor stabilization near target pads or semantic anchors, recent failed route movement, or repeated manual posture changes.
- **Auto-fill opportunity events**, such as field focus on a recognized schema, partial edit pause, repeated similar entries nearby, or a newly opened property panel with inferable defaults.
- **Suppression events**, such as rapid cursor movement, continuous routing motion, tool-mode churn, active modal dialogs, preview lease held elsewhere, or context versions changing faster than the latency budget can track.

This kind of coalescing mirrors how mature inline-completion systems avoid spamming providers on every keystroke. VS Code’s API explicitly frames `CancellationToken` as the mechanism for canceling long-running completion requests when the user keeps typing, and its inline-completion surface treats suggestions as ghost text rather than blocking interactions. The analogy is useful: KiSurf should treat Next Action previews like **workspace ghost actions**. If the user keeps moving, typing, dragging, or routing, cancellation is the default. citeturn4search0turn4search4turn4search15turn6view0turn6view3

I recommend the following runtime rules:

First, every `RuntimeStep` gets a cancellable token tied to the exact context version that created it. If `selection_version`, `tool_mode_version`, `viewport_version`, `ui_focus_version`, or `board_base_hash` changes in a way the packet marked as invalidating, the step moves immediately to `Expired` or `Superseded`, and any running hidden attempt must stop before further mutation. This is especially important because the branch already rejects stale accept by base hash; the scheduler should extend that same discipline to **pre-accept** work, not only accept time. citeturn6view2turn6view3

Second, the scheduler should explicitly distinguish **observe budget** from **attempt budget**. My recommended starting point is a very fast observe budget for deciding whether anything is worth doing at all, followed by a slightly larger hidden-attempt budget only when the user is stable enough and the likelihood of a good suggestion is high. In practice, that means most semantic events should end in `wait` or `abandon`, not in an attempt. This is the only way to keep the agent ambient rather than noisy. That recommendation is an engineering inference, but it is consistent with official agent guidance to keep systems simple and explicit, and with eval guidance that agent behavior must be measured across trajectories, not just single outputs. citeturn1search0turn2search1turn2search4turn2search14

Third, the scheduler should implement **preview hysteresis** to avoid flicker. Once a preview is visible, minor cursor tremors or small viewport shifts should not immediately tear it down and replace it. Instead, the runtime should keep the preview alive until an invalidating semantic event occurs, a better preview supersedes it, or the preview lease expires. This is one of the biggest differences between an editor-native assistant and a raw tool-calling agent: avoiding the perception that the UI is alive in a bad way.

## Hidden attempts and preview publication

The heart of the system is the hidden attempt protocol. This is where placement and routing become materially different from simple autofill, because geometry needs trial, observation, and correction. The current KiSurf branch already has the right ingredients: a semantic shadow board, typed atomic operations, checkpoints, rollback, validation, preview rendering, operation journals, and accept replay. KiCad itself also has native concepts that make this feasible in-process: `BOARD_COMMIT::Push()` executes changes, `BOARD_COMMIT::Revert()` restores modified item state, Undo/Redo are first-class in `PCB_EDIT_FRAME`, and `BOARD_COMMIT::MakeImage()` is explicitly tested to create a transient copy. citeturn6view1turn6view2turn6view3turn10view0turn10view1turn9view1turn9view2

The protocol I recommend is:

```text
Acquire preview lease
-> Create checkpoint from board_base_hash
-> Run one mutation batch on shadow board
-> Auto-render:
   - local crop
   - global viewport frame
   - overlay frame
   - before/after diff frame
-> Auto-validate:
   - DRC violations
   - clearance / keepout / overlap
   - connectivity
   - net/layer/via facts
   - layout-quality heuristics
-> Return AttemptRecord to LLM
-> ReviewDecision
-> rollback_retry / publish / abandon
```

An `AttemptRecord` should look like this:

```json
{
  "attempt_id": "uuid",
  "runtime_step_id": "uuid",
  "base_checkpoint_id": "uuid",
  "shadow_mutation_journal": ["op1", "op2", "op3"],
  "derived_entities": ["track:...", "via:...", "footprint_pose:..."],
  "render_outputs": {
    "focus_crop": "frame_ref",
    "overlay_frame": "frame_ref",
    "diff_frame": "frame_ref"
  },
  "validation_facts": {
    "drc_error_count": 0,
    "clearance_min_um": 180,
    "keepout_violations": 0,
    "connectivity_delta": "improved",
    "ratsnest_delta": -2,
    "overlap_count": 0,
    "via_count": 1
  },
  "provenance": {
    "tool_calls": ["router.plan", "shadow.apply_ops", "render.focus", "validate.local"],
    "wall_clock_ms": 312
  }
}
```

Two design choices are critical here.

The first is that a hidden attempt should usually contain **one side-effecting mutation batch**, not an unbounded chain of side-effecting tool calls. Read-only tool calls before or after that batch are fine, but keeping each attempt to one mutation batch makes checkpointing, replay, rollback, and eval grading much simpler. The current branch’s shift toward `kisurf_run_cell` plus session control tools and typed atomic lowering is compatible with that model. citeturn6view2turn6view4

The second is that the user should normally see **only the final published preview**, not the internal attempt sequence. The repository already draws a strong line between preview-only autonomous suggestions and accepted execution sessions, and KiCad’s overlay and preview infrastructure is well suited to non-mutating visualization through `VIEW_OVERLAY`, `KIGFX::PREVIEW::DRAW_CONTEXT`, and diff-style render contexts. Hidden attempts should therefore be stored in observability traces and surfaced in devtools or evaluation mode, but not animated in the normal user experience except in carefully chosen explainability modes. citeturn6view3turn13view0turn13view1turn13view2turn11search17

This is also where placement, routing, and autofill diverge.

For **placement**, the runtime should favor anchor-oriented attempts. The current branch already exposes semantic placement candidate anchors around the cursor and highlights them in visual reads, which is exactly the abstraction the hidden-attempt loop needs. The LLM should choose among anchor hypotheses, request a hidden placement pose, then review overlap, keepout, local spacing, relevant-net proximity, ratsnest effect, and visual fit. If the pose is poor, it should retry with a different anchor bias or orientation hint rather than descending into raw coordinate micromanagement too early. citeturn6view3turn0search3turn0search18

For **routing**, the runtime should usually use router-as-tool or candidate-path-as-tool rather than raw coordinate stepping. KiCad’s router continuously observes DRC while routing, supports shove and walk-around modes, respects net-class and custom-rule geometry, and handles differential-pair gap behavior from design rules. That means the LLM should typically decide the routing *intent* and comparison criteria, while the router generates route geometry in shadow state. The model then reviews not just legality but fit to intent: via count, layer usage, posture, path directness, coupling behavior, obstacle handling, and whether the result preserves future routing space. Only in tight or ambiguous contexts should the model descend to anchor- or waypoint-level guidance. citeturn12search3turn12search5turn12search7turn12search8turn12search11turn12search13

For **auto-fill/refill**, hidden attempts are much lighter. The “shadow mutation” may be a schema-bound field patch rather than geometry. The review loop still matters, but it is mostly about schema validity, project consistency, nearby-value consistency, and overwrite safety rather than rendered geometry. The same step contract still works; the attempt just becomes faster and cheaper.

## Visual and structured observation design

For an LLM-mediated editor agent, observation quality determines whether self-review is real or performative. KiSurf should not force the model to infer everything from pixels, and it should not assume structured state alone is enough. The observation packet should always combine **visual frames** with **structured facts**. This is strongly supported both by the repository’s current tooling and by KiCad’s rendering stack. KiSurf already exposes workspace view, visual frame metadata and pixels when available, automatic routing/placement render directives, anchor markers, and diagnostics for missing pixels. KiCad’s graphics layer includes `VIEW`, `VIEW_OVERLAY`, a dedicated preview draw context, and even a diff-canvas context that color-overrides and dims items by UUID. citeturn6view2turn6view3turn13view0turn13view1turn13view2turn11search17

The best long-term observation stack has four image-like layers and one fact layer:

**Full viewport frame.** This is for situational awareness: what region is on screen, what the tool posture feels like, whether the local geometry is crowded, and whether the suggestion would be visually disruptive.

**Focus crop.** This is the most important frame for placement and routing review. It should tightly crop around the hidden attempt and its relevant anchors, pads, nets, and obstacles.

**Overlay frame.** This should include non-mutating semantic annotations: candidate anchors, active route start/end, proposed path, keepout halos, collision outlines, important net labels, or field-diff highlights.

**Diff frame.** This is the before/after reviewer frame. KiCad already has enough preview and diff-oriented building blocks that KiSurf should treat “attempt diff render” as a first-class output, not as an afterthought. citeturn13view0turn13view1turn13view2

**Structured fact layer.** This should summarize what the LLM should never be forced to read from pixels: net names, active layer, min clearance observed, overlap count, connected/unconnected endpoints, list of violated rules, route length, via count, candidate anchor IDs, table schema, editable field semantics, and whether the preview is now stale.

In practice, each work state should prioritize those layers differently. Placement leans heavily on focus crop plus overlayed anchors and structured overlap/keepout facts. Routing leans on focus crop plus before/after diff plus structured DRC/connectivity facts. Autofill may need no pixels at all in many cases, but should still be able to request a UI crop when panel layout or nearby context matters.

The important implementation detail is that **observation should be tool-mediated too**. The model should request “focused route review around attempt A” or “table fill review with neighboring rows,” not ask for arbitrary screenshots. That keeps visual observation composable, cacheable, and evaluable.

## Session isolation and preview leasing

Chat Agent and Next Action Agent should share one **session core**, but they should not share one undifferentiated runtime namespace. The repository already says the two workflows share underlying editor context, preview, validation, and operation infrastructure while serving different product roles. That should become a formal isolation model rather than an informal convention. citeturn6view3turn6view4

I recommend four separate coordination primitives.

First, a **session namespace**. Every execution context should live under a namespace such as `chat/<thread_id>` or `nextaction/<editor_id>/<suggestion_id>`. Chat namespaces may have richer permissions, longer budgets, and larger journals. Next Action namespaces should be ephemeral, tightly budgeted, and preview-first by default.

Second, a **preview lease**. Only one workflow should own the primary preview surface in a given editor at a time. If the Chat Agent is in the middle of a user-invoked previewing session, Next Action may continue observing in the background but should not publish over it. If Next Action currently holds the lease, a user-invoked Chat preview should preempt it. Lease transfer should be explicit and logged.

Third, an **accept ownership token**. Accepting a preview should require a token that binds together `preview_id`, `board_base_hash`, `lease_id`, `owner_namespace`, and `expiry`. This makes it impossible for a stale or superseded preview to commit just because the user clicked accept slightly later. The current branch already uses accept replay and rejects stale accept by base hash; the ownership token generalizes that into a stronger multi-agent contract. citeturn6view2turn6view3

Fourth, a **supersede/cancel policy**. The runtime state machine should explicitly support:

- `Observed`
- `Reasoning`
- `Attempting`
- `Reviewing`
- `Retrying`
- `Published`
- `Accepted`
- `Rejected`
- `Expired`
- `Superseded`
- `Abandoned`

The important distinction is between `Expired` and `Superseded`. `Expired` means the world changed and the preview is no longer valid. `Superseded` means a newer suggestion for the same opportunity replaced it. That distinction matters for metrics and for the user experience: a superseded preview may transition smoothly to its replacement, while an expired preview should disappear and possibly leave a trace in logs only.

Human-in-the-loop runtime patterns from frameworks like LangGraph are useful analogies here. Interrupts and persistence show the value of durable state around approval boundaries, but KiSurf should implement those ideas natively in C++ around editor sessions, checkpoints, leases, and accepts, not as a generic workflow engine bolted on later. citeturn1search2turn1search6turn6view1turn6view3

## Eval harness and rollout gates

The right eval harness for KiSurf is not just “did the tool run?” It has to measure whether the **runtime loop** behaved correctly and whether the resulting suggestion was actually helpful. Anthropic’s recent guidance is especially relevant: agent evals are harder because the system acts over many turns, calls tools, modifies state, and can fail at trajectory level rather than only at final-answer level. OpenAI’s current eval guidance similarly emphasizes traces, graders, datasets, and repeated evaluation over a variable generative system. citeturn2search1turn2search4turn2search7turn2search14turn2search0

KiSurf is already in a better starting position than many editors because the branch has observability logs for user input, model input/output, tool calls, tool results, and suggestions, plus a native semantic self-test surface. Those should be treated as the seed of the eval harness. citeturn6view2turn6view3

The harness should have three layers.

**Contract evals.** These are deterministic and fast. They verify that every `ObservationPacket`, `RuntimeStep`, `AttemptRecord`, `ReviewDecision`, and `PublishDecision` conforms to schema; that every attempt has a valid checkpoint; that rollback fully restores shadow state; that publish cannot happen without review; that accept fails on stale base hash; that replay produces one undoable editor commit; and that tools never publish directly. These tests should run in CI on every runtime change. The repository’s shift toward typed atomic operations, journals, checkpoints, and replayable property patches is exactly what makes this possible. citeturn6view2turn6view4turn10view0turn10view1

**Trajectory evals.** These replay recorded semantic-event traces against frozen board snapshots. The grader looks at full runs, not isolated outputs. Good trajectory metrics include observe latency, attempt latency, stale rate, cancellation correctness, retry success rate, publish rate, accept rate, supersede rate, rollback correctness, and flicker count. For routing and placement, deterministic graders should also score DRC delta, overlap count, connectivity improvement, via count, path length, and whether the published preview remained valid until accept or intentional expiration. citeturn2search1turn2search4turn2search14turn6view3

**Usefulness evals.** These are hybrid. Deterministic graders can answer “was it legal?” and “was it stale?” but not always “was it the right next action?” For that, KiSurf should add curated human labels and model-graded rubrics around helpfulness, intent fit, intrusiveness, and preview clarity. OpenAI’s and Anthropic’s eval guidance both support mixing objective and model-based graders, especially for nuanced agent behaviors. citeturn2search0turn2search1turn2search4turn2search7

The production dashboard should at minimum track these metrics over time:

- **Opportunity precision**: how often published suggestions are accepted or meaningfully interacted with.
- **Noise rate**: suggestions per minute that are rejected, immediately expired, or ignored.
- **Staleness rate**: previews invalidated before accept because the context changed.
- **Latency**: semantic event to observe decision, and semantic event to published preview.
- **Retry value**: how often a second hidden attempt converts a failure into a publishable suggestion.
- **Flicker**: preview disappear/reappear cycles per session.
- **Commit survival**: accepted previews later undone by the user.
- **Routing quality**: DRC-clean publish rate, via count, path directness, future blockage heuristics.
- **Placement quality**: overlap-free publish rate, keepout compliance, ratsnest improvement, locality fit.
- **Autofill quality**: schema-valid publish rate, overwrite mistake rate, consistency correction rate.

For rollout, I would gate capabilities in this order:

```text
Contract correctness
-> Stable cancellation / no stale publish
-> Useful autofill
-> Anchor-based placement previews
-> Route-to-anchor previews
-> Hidden placement retries
-> Hidden routing retries
-> Broader autonomous next actions
```

That sequence matches the repository’s current maturity: preview-only Next Action plumbing exists now; anchor-based route previews and placement candidate anchors already exist now; production-quality autonomous placement and routing do not yet. A good eval harness should therefore first prove the loop is safe and debuggable before chasing higher acceptance rates. citeturn6view0turn6view2turn6view3

The short version is this: the most durable KiSurf architecture is not “more native policy” and not “more latent magic inside tools.” It is a **typed, versioned, cancellable, replayable LLM step protocol** built on top of KiCad’s in-process commit/preview infrastructure and KiSurf’s shadow-session runtime. If every semantic step goes through that contract, the LLM remains the brain, the tools remain the body, and Next Action suggestions can become both more capable and more trustworthy over time. citeturn6view1turn6view2turn6view3turn10view0turn13view0turn13view2turn2search0turn1search1