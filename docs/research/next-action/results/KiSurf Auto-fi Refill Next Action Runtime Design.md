# KiSurf Auto-fill Refill Next Action Runtime Design

## Recommended direction

KiSurf’s strongest architectural asset is that it already defines Auto-fill / Refill as a **work state inside the same unified Next Action Runtime** that handles observe → decide → hidden attempt → render/validate → review → publish/abandon, rather than as a separate assistant pipeline. The repository README is unusually explicit on this point: the Next Action Agent is a unified, LLM-mediated runtime; placement, routing, and auto-filling/refilling are all work states inside that one runtime; tools may generate placement, routing, and panel-fill candidates, but they are not allowed to publish previews on their own; and published previews already carry provenance, preview-lease, and accept-token metadata that are checked again at accept time. That means the right path is not to create a second autofill agent, but to add a **structured-surface work state** that plugs into the existing semantic-event, shadow-state, preview-first, accept-gated machinery. citeturn22view4turn22view0turn22view1turn22view2turn22view3

That recommendation also matches KiSurf’s product stance. The repo states that the product goal is to make the “next reasonable engineering step” visible and easy to accept; suggestions should be reviewable edits in the actual workspace; the engineer remains in control; AI-native surfaces should appear where real work happens, including inspectors, rule editors, design review panels, command palettes, and contextual workflows; and the foundation already includes preview-only Next Action plumbing, activity-triggered suggestions, observability logs, and semantic panel state. In other words, Auto-fill / Refill should behave like **ambient, structured-edit assistance**, not like a form-filling chatbot off to the side. citeturn20view0turn21view0turn22view4

KiCad’s own UI surfaces make this design especially important. Official documentation describes the Symbol Properties window as a table of symbol fields where fields can be added, edited, reordered, and resized, and notes that symbol fields propagate to the corresponding footprint when the PCB is updated from the schematic. The Symbol Fields Table is both a bulk-edit surface and a BOM tool whose exported fields match the spreadsheet view exactly. In the PCB editor, footprint fields are named fields rather than plain text, can be edited in the Footprint Properties dialog, and are synced between footprints and corresponding schematic symbols. Board Setup contains a Net Classes table with rule values such as clearance, track width, via sizes, and differential-pair sizes, plus pattern-based netclass assignments and a custom-rules text editor with syntax checking. These are precisely the kinds of structured, interdependent surfaces where an always-on ambient assistant can save time—but only if it is revision-aware, validation-aware, and careful about not overwriting deliberate user choices. citeturn9view1turn9view0turn9view3turn10view4turn9view4turn9view5turn9view6

My bottom-line recommendation is therefore:

1. Add a **Structured Surface Adapter layer** under the current Next Action Runtime.
2. Introduce an **Auto-fill / Refill episode type** that uses the same shadow apply, render/validate, LLM review, publish, and accept path as placement and routing.
3. Make the LLM the semantic decider over **scope** and **intent**—one cell, a row, a column, a region, or a property group—while deterministic tools contribute schema, facts, candidates, normalization, validation, and replay-safe patch execution.
4. Default to **fill-empty-first, preview-first, explanation-visible, accept-gated, and stale-on-change** behavior.

## Activation model

The most important activation principle is that **hidden planning should wake more often than visible publishing**. Human-AI interaction guidance says AI systems should time services based on the user’s current task and environment, and modern suggestion systems increasingly gate display on expected utility rather than simply on availability. In AI-assisted programming, suggestions often appear either on explicit invocation or when the user pauses; more recent work shows that selectively hiding suggestions that are likely to be rejected can reduce both latency and verification cost. Meanwhile, a 2026 spreadsheet “next action” benchmark shows a useful tradeoff: predicting after every user action can maximize user actions saved, even when acceptance rate is lower, because the system sees more opportunities; acceptance heuristics then determine what should actually surface. For KiSurf, that implies a two-stage policy: **wake cheaply and often; publish rarely and deliberately**. citeturn27view5turn17view1turn17view0turn13view4turn13view5

For Auto-fill / Refill, I recommend three trigger classes. The first is **prefetch triggers**, which should open or refresh hidden planning immediately but not necessarily publish a suggestion yet. These include: focused cell changed, selected row or range changed, property panel opened, validation error surfaced, related schematic/board context changed, accepted placement or routing edit that can make downstream fields inferable, and value deletion that leaves a field empty. These events are semantically meaningful, but many of them are ambiguous until the user pauses or stabilizes their focus. The KiSurf repo already coalesces raw editor events into semantic events before opening Next Action episodes, and the current runtime already has panel-fill candidates and activity logs, which makes this trigger class a natural fit. Cross-surface propagation matters here because KiCad field data is coupled across schematic, PCB, BOM, and rules surfaces. citeturn22view4turn22view0turn9view1turn10view4turn9view0turn9view4

The second class is **publish-eligible triggers**, where hidden planning should begin immediately and a visible preview may publish after a short stability window if the LLM decides the suggestion is timely and helpful. These include: stable focus in an empty or invalid cell; stable selection of a row, region, or property group; a committed deletion that reveals inferable empty values; a selected validation issue whose likely repair is a structured fill; a property panel opened and held in focus for a brief dwell interval; and an accepted placement/routing suggestion that creates obvious downstream inference opportunities such as footprint fields, netclass assignments, rule-area settings, or BOM-affecting metadata. This is consistent with timing guidance from HAX Guideline 3 and with the “show only when utility is positive” framing from the Copilot display paper. citeturn27view5turn17view3

The third class is **commit-required triggers**, which should not publish at all until a semantic commit or stable-focus event occurs. These include: active typing inside a cell or text field, drag-selection still in flight, paste operation still streaming, dialog edits before the user presses OK/Apply, and schema-changing operations that have not yet committed. Here the correct system behavior is to keep gathering context, but wait to publish until the user has either paused, finished the gesture, committed the dialog, or explicitly asked for help. Explicit user requests are a special case: they should bypass debounce and immediately open a publish-eligible episode. That is consistent with both mixed-initiative principles—balancing automation with direct manipulation—and with HAX Guideline 7 on efficient invocation. citeturn16view1turn28view3

In practice, I would implement the policy as:

```text
Raw UI events
  -> semantic trigger coalescer
  -> autofill utility gate
  -> hidden planning episode
  -> optional preview publish
```

The utility gate should consider: stability of focus, overlap between target scope and current user gesture, predicted acceptability, validation severity, whether the target values are empty/default/inherited, and whether the episode is likely to save more work than it creates. The key design rule is simple: **always-on does not mean always-visible**. citeturn22view4turn17view0turn13view5

## Observation packet and patch model

For structured surfaces, the LLM should receive a **compact, typed observation packet**, not a vague textual summary. SpreadsheetLLM is helpful here because it shows that large models do materially better on structured surfaces when the representation includes **addresses, values, and formats**, and when repeated patterns and sparse structure are compressed rather than naively serialized. Its compression ideas—structural anchors, inverted-index style aggregation of repeated values, and data-format-aware aggregation—map well to KiSurf tables, grids, dialogs, and property groups. For KiSurf, the lesson is not “turn forms into spreadsheets,” but “send the model a schema-backed slice with typed neighborhood context, not a flattened blob.” citeturn14view0turn14view1turn14view2turn14view3turn14view4

I recommend the following packet shape:

```json
{
  "episode": {
    "id": "naf_...",
    "trigger": "field_deleted | stable_focus | dialog_committed | validation_selected | context_changed",
    "surface_id": "pcb.board_setup.netclasses",
    "surface_kind": "table | property_panel | dialog | grid | form",
    "base_revision": "rev_hash",
    "schema_version": "schema_hash",
    "selection_fingerprint": "sel_hash",
    "stable_ms": 240
  },
  "target": {
    "scope_hint": "cell | row | column | region | property_group | abstain",
    "focused_field": "clearance",
    "selected_rows": ["RAM_ADDR", "SPI"],
    "selected_range": null
  },
  "schema": {
    "fields": [
      {
        "id": "clearance",
        "label": "Copper Clearance",
        "type": "dimension",
        "units": ["mm", "mil"],
        "enum_options": null,
        "readonly": false,
        "nullable": false,
        "computed": false,
        "default_policy": "inherit:constraints.minimum_clearance",
        "dependencies": ["constraints.minimum_clearance"]
      }
    ]
  },
  "values": {
    "target_values": {...},
    "neighbor_rows": [...],
    "neighbor_columns": [...],
    "inherited_values": {...},
    "defaults": {...},
    "validation_state": {...}
  },
  "context": {
    "project": {...},
    "schematic": {...},
    "board": {...},
    "nets": {...},
    "components": {...},
    "footprints": {...},
    "recent_user_edits": [...],
    "recent_accepts_rejects": [...]
  },
  "provenance": {
    "field_origins": {
      "clearance": {
        "origin": "user_authored | inherited | imported | ai_accepted | defaulted",
        "source_ref": "...",
        "confidence": 0.92
      }
    }
  },
  "candidates": {
    "normalized_units": {...},
    "enum_candidates": {...},
    "repeat_patterns": {...},
    "rule_conflicts": [...]
  }
}
```

This packet should be built by a **surface adapter** plus a **project-context composer**. The surface adapter owns only representation and mutation mechanics; it does not decide what to publish. The adapter should expose surface-local schema, range/value slicing, neighbor extraction, readonly/computed flags, defaults, and validation hooks. The project-context composer then joins in cross-surface facts: schematic-to-footprint linkage, board constraints, DRC/ERC findings, rule areas, text variables, BOM implications, and recent accepted placement/routing edits that may have made these fields inferable. That design is strongly aligned with KiSurf’s current model-facing tools for context snapshots, workspace view, validation, session control, observability, and guarded semantic UI actions. citeturn22view4

On the mutation side, the right abstraction is a **bounded patch plan** that the LLM chooses semantically and deterministic code executes safely. SheetCopilot’s main contribution here is not that spreadsheets are the same as PCB editors, but that LLM-driven software control becomes tractable when actions are factored into a set of atomic operations and orchestrated through a robust state machine. KiSurf’s own README points in the same direction: complex editing is being funneled through typed atomic operations, inspectable/replayable property patches, hidden shadow state, rollback, and render/validation rather than through ad hoc model-facing bespoke tools. For an ambient always-on agent, I would go one step further and **forbid unconstrained scripting at publish time**: ambient Auto-fill / Refill should emit a declarative `PatchPlan`, which the runtime lowers into typed atomic ops. citeturn14view6turn14view5turn21view0turn22view4

The tool stack should have three layers. The first is **atomic tools**: query surface schema, query values, set cell/property, set range, validate shadow patch, rollback shadow patch, render diff, and compute overlap/staleness. The second is **integrated tools**: infer row values, infer column values, infer property-group values, normalize units, suggest enum values, detect repeated patterns, and validate against project rules. The third is a **bounded batch layer**: loops and conditions only over an explicitly selected region or property set, with hard operation budgets and no invisible side effects. The LLM decides whether the target is one cell, a row, a region, or a group; deterministic tools then carry out the plan in shadow state, validate it, and hand the result back to the LLM for review. That preserves your premise that the LLM is the semantic decider without making it the uncontrolled executor of arbitrary mutation code. citeturn22view4turn14view6turn14view5

A representative plan object would look like this:

```json
{
  "surface_id": "eeschema.symbol_fields_table",
  "scope": { "kind": "row_set", "rows": ["U3", "U4"], "overwrite_mode": "fill_empty_only" },
  "intent": "refill footprint and supplier fields from project/library context",
  "steps": [
    {
      "for_each": "row in selected_rows",
      "if": "empty(row.Footprint)",
      "set": {
        "Footprint": { "tool": "infer_row_values", "field": "Footprint" }
      }
    },
    {
      "for_each": "row in selected_rows",
      "if": "empty(row.MPN)",
      "set": {
        "MPN": { "tool": "infer_row_values", "field": "MPN" }
      }
    }
  ],
  "review_policy": {
    "require_validation": true,
    "publish_only_if": "no_schema_errors && no_hard_rule_conflicts"
  }
}
```

This gives the LLM meaningful control over semantics and scope, but keeps execution bounded, inspectable, replayable, and easy to reject, roll back, or revalidate. citeturn21view0turn22view0

## Preview and acceptance model

The right preview model is **surface-native, granular, and accept-gated**. KiSurf’s repo already emphasizes reviewable edits in place, lightweight acceptance, preview-first publishing, and guarded accept paths. Modern IDE copilots add a useful interaction vocabulary on top: GitHub Copilot and VS Code describe inline ghost text suggestions and next-edit suggestions; ghost text appears inline as you type; next-edit suggestions predict both where the next edit should be and what it should be; and `Tab` is the canonical accept gesture. CLion adds a valuable extra detail: suggestions can be accepted not only all at once, but also word by word or line by line. For KiSurf, the equivalent is clear: allow acceptance at the **cell, row, region, or property-group** level rather than only “apply everything” or “dismiss everything.” citeturn18view0turn18view1turn18view2

That maps neatly to KiSurf’s structured surfaces:

**For single cells or single properties**, show dimmed inline ghost text or a value chip directly in the field. The affordance should be low-noise and keyboard-first. If the candidate is a normalization rather than a new value, show a compact side-by-side normalization preview such as `10mil → 0.254 mm`. citeturn18view0turn18view1

**For rows, columns, and regions**, show a structured diff overlay rather than repeated ghost text in every cell. Use per-cell highlights, with row- and region-level accept affordances. A row-level preview should summarize: “3 fields proposed, 1 inherited, 0 rule conflicts.” A region-level preview should allow the user to accept all, accept selected cells, or collapse to a summary if the suggestion is large. This is the structured-surface analogue of next-edit suggestions spanning multiple symbols or lines. citeturn18view0turn18view1

**For property panels and dialogs**, show a grouped diff: field name, current value, proposed value, provenance, confidence band, and validation markers. The most important properties should stay inline; the explanation can be hover- or disclosure-based. HAX Guideline 11 says users should be able to access an explanation of why the system behaved as it did, and W3C PROV defines provenance precisely as the information about entities, activities, and people involved in producing a result that allows assessment of reliability and trustworthiness. In KiSurf terms, every visible proposal should answer “why this?” with sources like inherited default, schematic linkage, matching library footprint, repeated row pattern, or project text variable. citeturn27view2turn11view7

For rejection and backoff, the HAX guidelines are directly on point: support efficient dismissal, support efficient correction, scope services when in doubt, remember recent interactions, learn from user behavior, and update/adapt cautiously. I recommend three explicit negative actions: **Reject once**, **Ignore this field/scope for now**, and **Mute this suggestion pattern**. “Reject once” should cancel the current preview only. “Ignore this field/scope” should suppress similar suggestions for a short-lived session window keyed by surface id, field id, and selection shape. “Mute this suggestion pattern” should create a longer-lived preference, for example “do not refill manufacturer fields automatically in this table unless explicitly asked.” The runtime should also learn from behavior: repeated reject-or-edit-after-accept patterns should lower publish aggressiveness for that scope, but adaptation must be cautious and observable rather than disruptive. citeturn27view1turn28view3turn27view3turn28view1turn28view0

My recommended acceptance vocabulary is:

```text
Tab / Enter        accept focused cell or property
Shift+Tab          accept previous pending cell/property
Ctrl+Enter         accept row or property group
Alt+Enter          accept region
Esc                reject current preview
⌘. / context menu  accept chosen subset / ignore / mute pattern
```

The exact keymap can change, but the semantic model should not: **one accelerator for the smallest scope, larger accelerators for broader scopes, and a single universal reject gesture**. That preserves flow without making Auto-fill / Refill feel sticky or invasive. citeturn18view1turn18view2turn27view1

## Staleness, overwrite safety, and provenance

KiSurf already has most of the conceptual scaffolding needed for safe acceptance. The repo states that suggestions are tied to the editor revision, selection, tool state, and viewport; published previews carry preview leases and accept tokens; accept revalidates token, lease, and context; and stale accept is rejected by base hash. That should become the core safety contract for Auto-fill / Refill as well. Every preview should carry a **surface revision token**, **schema version**, **selection fingerprint**, **related-object digest**, and **overlap set** of cells/properties it intends to change. If any of those change materially before accept, the preview should expire rather than trying to “helpfully” merge itself into a new context. citeturn22view4turn22view1turn22view3

For conflict handling, borrow the logic of optimistic concurrency. MDN’s explanation of conditional requests is a clean analogy: multiple clients may update the same object in parallel; updates based on obsolete versions should be rejected; and when rejection happens, the client can notify the user or show a diff to reconcile changes. KiSurf does not need HTTP headers to implement this, but it should implement the same concept: accept only if the preview’s base revision and target overlap still match current state, otherwise reject with a **refreshable diff**. This is especially important once you consider simultaneous edits from the user, another AI session, or cross-surface updates triggered by accepted placement/routing actions. citeturn11view8turn22view3

The most important overwrite rule is: **user-authored values are preserved by default**. HAX’s “update and adapt cautiously” and “support efficient correction” guidelines support this, and KiSurf’s own engineer-in-control stance points the same way. I recommend explicit value-origin tags on every mutable field:

```text
user_authored
imported_external
project_default
inherited
deterministic_propagated
ai_accepted
```

Ambient Auto-fill / Refill should normally operate in `fill_empty_only` mode and may overwrite only `project_default`, `inherited`, or previous `ai_accepted` values without an extra user escalation. Overwriting `user_authored` or `imported_external` values should require either an explicit user request, a very strong structured reason such as “this is schema-invalid and blocks rule compliance,” or a larger-scope accept gesture that makes the replacement obvious. This is the single most important guard against the “Flash Fill changed my already-correct data” failure mode users complain about in broad autofill systems. citeturn28view0turn28view3turn15view3turn4search13

Confidence should be shown, but not worshipped. The provenance literature is clear that provenance supports judgments about reliability and trustworthiness; human-AI interaction guidance is equally clear that systems should make clear what they can do, how well they can do it, and why they did what they did. For KiSurf, that means confidence belongs in the UI as a **secondary signal**, backed by provenance and validation state, not as a primary authority indicator. I would compute it from a small, auditable vector: schema fit, validator results, source quality, cross-surface consistency, repeat-pattern strength, and whether the value is a conservative fill-empty inference or a proposed overwrite. The UI then shows a compact confidence band and an explanation disclosure, for example: “High confidence: exact library match + netclass rule fit + no validation conflicts.” citeturn11view7turn27view0turn27view2

Finally, previews should have an explicit **lease horizon**. The lease should end on any of the following: focus leaves the target surface in a materially different way; the user edits an overlapping target; schema changes; project context changes in a way that affects inference; selection changes enough to invalidate scope; or another session modifies the same related objects. If a preview merely becomes less relevant but not invalid—for example the user scrolls inside the same panel—it can stay hidden and dormant, but it should not silently reappear without re-review. The correct bias is to expire aggressively and regenerate cheaply, because KiSurf already has semantic-event coalescing and observability infrastructure that supports frequent re-episodes. citeturn22view4turn22view1

## Evaluation model

The best evaluation model for Auto-fill / Refill is **online, sequential, and acceptance-aware**, not static “top-1 suggestion accuracy.” The 2026 spreadsheet next-action benchmark is especially relevant because it evaluates a system after each user action, accepts or rejects predictions, updates the future when suggestions are accepted, and measures both precision and “user actions saved.” The paper argues that this online framing is better than teacher-forced offline scoring because suggestions alter the state from which later work proceeds, false positives can compound, and the system has to deal with its own prior suggestions. That is almost exactly the setting KiSurf’s always-on Next Action Runtime will operate in. citeturn13view0turn13view1turn13view5turn13view6

I recommend organizing metrics into four groups.

**Patch correctness.** Measure schema-valid patch rate, validator-pass rate, project-rule-valid patch rate, and partial-validity rate for larger previews that contain both correct and incorrect cells. In SheetCopilot terms, this is the distinction between “executes without exception” and “functionally correct,” which is still useful once translated from spreadsheets to PCB editing surfaces. citeturn14view5

**User-value protection.** Measure deliberate-user-value preservation rate, protected-overwrite avoided rate, stale-accept rejection correctness, and post-accept correction rate. The highest-risk failure for ambient autofill is not merely “wrong value,” but “wrong value that replaced something the engineer meant.” citeturn22view3turn28view3

**Interaction utility.** Measure publish rate, accept rate, reject rate, ignore/mute rate, user actions saved, time to first useful preview, partial-accept rate, and backoff effectiveness after repeated rejects. The spreadsheet next-action benchmark’s `user actions saved` concept is especially valuable because a suggestion can be high precision but still not save meaningful work. citeturn13view5

**Trace and replay quality.** Measure undo correctness, redo correctness, journal replay determinism, provenance completeness, and observability trace quality. KiSurf already exposes observability logs and replayable typed property patches, so Auto-fill / Refill should inherit that auditability rather than inventing new logging semantics. citeturn22view4turn22view0

The test plan should mirror those metrics. Use deterministic unit tests for schema extraction, unit normalization, range lowering, validation integration, and lease invalidation. Use editor integration tests for each surface family: symbol fields tables, footprint properties, BOM-related tables, netclass forms, rule/configuration panels, and dialogs. Most importantly, add **replay tests built from real edit traces**: feed the runtime a recorded sequence of semantic surface events, ask it for predictions at each step, accept or reject them under different policies, and compare final state, correction burden, and overwrite safety. That is where the online next-action benchmark is most valuable as inspiration. citeturn13view0turn13view5turn13view6

A concrete release gate for the feature should be:

- high schema-valid patch rate on targeted surfaces,
- near-zero protected-user-value overwrites,
- strong stale-accept rejection correctness,
- measurable positive user-actions-saved,
- low correction-after-accept on accepted previews,
- deterministic undo/redo and replay,
- and complete provenance on every published suggestion.

Those are much better ship criteria than “the model seems smart in demos.” citeturn21view0turn22view4turn13view5

## Concrete architecture for KiSurf

The architecture I recommend is a **Structured Surface work state** inside the existing Next Action Runtime, implemented with five new runtime components and one new data contract.

The first component is a **Surface Adapter Registry**. Each structured editor surface—table, property panel, dialog, grid, rules form, BOM-related sheet, footprint field editor—registers an adapter that can expose schema, values, selected scope, defaults, inherited values, readonly/computed state, validators, render hooks, and atomic mutation methods. This keeps surface mechanics local, while preserving one shared runtime contract across all surfaces. It is the exact analogue of how KiSurf already uses common tooling across placement and routing while keeping those as distinct work states rather than separate agents. citeturn22view4

The second component is a **Semantic Trigger Classifier**. Raw UI activity should continue to be coalesced into semantic events, but Auto-fill / Refill needs a structured trigger vocabulary: `stable_focus`, `selection_stabilized`, `field_cleared`, `panel_opened`, `dialog_committed`, `validation_selected`, `related_context_changed`, `downstream_inference_available`, and `explicit_request`. Each semantic trigger gets a wake policy: prefetch-only, publish-eligible, or commit-required. This fits naturally into KiSurf’s current Next Action episode opening model. citeturn22view4

The third component is an **Observation Builder**. Given a semantic trigger and a surface adapter, it builds the typed packet described earlier. It should compress the packet around the selected scope and nearest explanatory neighborhood using SpreadsheetLLM-style address/value/format encoding rather than full-surface dumps. It should also join in project context from existing KiSurf tools: workspace snapshot, activity timeline, context snapshots, validation results, and related object metadata. citeturn14view0turn14view4turn22view4

The fourth component is a **PatchPlan Runtime**. The LLM emits one of two things: `abstain`, or a bounded `PatchPlan` with an intended target scope, overwrite mode, and proposed steps. Deterministic tools then lower that plan into typed atomic patch ops, apply them to a shadow surface, validate, render a diff, and return the full reviewed candidate back into the existing runtime loop. This mirrors the current KiSurf direction away from bespoke model-facing mutation tools and toward typed atomic operations, journaled patching, and explicit render/validation. citeturn21view0turn22view4

The fifth component is a **Preview Lease Manager**. Every published preview should include base revision, schema version, selection fingerprint, overlap set, provenance bundle, and accept token. Accept must compare those against current state before application. If the preview is stale, the runtime should reject accept and offer a refreshed diff, not silently adapt it. The repository already has the underlying preview lease, accept token, and base-hash rejection concepts; Auto-fill / Refill should reuse them exactly instead of inventing surface-specific safety rules. citeturn22view1turn22view2turn22view3

The new data contract is **SurfacePatch**. This is the accepted, replayable, undoable unit for structured-surface changes. A `SurfacePatch` should contain the normalized target operations, provenance per field, validation output, and a human-readable explanation bundle. On accept, it becomes one undoable editor commit or one grouped commit if you later choose to support multi-surface coordinated accepts. The repo’s emphasis on inspectable, replayable patches and accepted journals makes this a clean fit. citeturn21view0turn22view0

In operational terms, the runtime becomes:

```text
semantic event
  -> surface adapter lookup
  -> observation packet build
  -> LLM decides: abstain | cell | row | column | region | property_group
  -> tools gather facts / candidates / normalization
  -> bounded PatchPlan lowered to typed atomic ops
  -> shadow apply
  -> render diff + validate
  -> LLM review
  -> publish preview with lease + accept token
  -> guarded accept path or expire/rollback/abandon
```

That is the concrete architecture I would ship. It preserves KiSurf’s existing philosophy—LLM-mediated, preview-first, accept-gated, unified runtime, engineer-in-control—while giving Auto-fill / Refill the structure it needs to succeed on real editor surfaces. It also scales: once the Surface Adapter contract exists, adding a new structured surface becomes mostly an adapter and schema problem, not a new-agent problem. citeturn22view4turn21view0

The most important implementation rule to keep throughout is this: **the LLM chooses the semantic scope and proposed values; deterministic code owns facts, validation, bounded execution, and replayability; only the reviewed runtime may publish**. That rule is already visible in KiSurf’s current README-backed design, and it is exactly the rule that will keep Auto-fill / Refill useful instead of intrusive. citeturn22view4turn21view0