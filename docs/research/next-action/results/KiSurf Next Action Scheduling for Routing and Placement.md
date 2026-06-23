# KiSurf Next Action Scheduling for Routing and Placement

## Bottom line

The strongest design is a **hybrid**: detect active routing and placement states deterministically, start **proactive hidden planning immediately** once the semantic head or attached object is known, and make **publication stability-aware** so previews appear only when they are still relevant and unlikely to fight the engineer’s hand. In other words, **plan like Strategy B, publish like Strategy A**. That hybrid is the closest fit to KiSurf’s own product direction: the repo describes the Next Action Agent as an ambient background workflow that proposes the next useful in-workspace action, ties suggestions to editor revision/selection/tool state/viewport, runs semantic-event episodes through hidden attempt → render/validation → LLM review → publish/abandon, and already exposes routing start anchors plus placement candidate anchors as deterministic semantic context. citeturn23view0

Pure Strategy A is safer, but too conservative as the primary mechanism for an AI-native “next action” assistant. Pure Strategy B is closer to KiSurf’s identity, but if publication is not gated it will create exactly the kind of mid-motion interference that proactive-assistant research warns against. Recent developer-IDE research found that proactive interventions do best at **workflow boundaries**, while mid-task interventions were often dismissed; other proactive-assistant work similarly shows that **timing and delivery mode** matter as much as the suggestion itself. citeturn26academia8turn10search1turn10search2

A concise recommendation follows:

| Dimension | Stability-gated preview | Proactive next-landing | Recommended reading |
|---|---|---|---|
| Product fit | Good for caution and polish | Best match for “next action” identity | **Hybrid wins**: proactive compute, stability-aware publish. citeturn23view0turn26academia8 |
| Hand-feel during fast work | Least disruptive | Can feel competitive if shown mid-motion | Keep B’s hidden planning, A’s publication gate. citeturn10search1turn26academia8 |
| Latency tolerance at 200–800 ms | Worse, because compute starts late | Better, because compute overlaps hand motion | Start early, publish only if still current. citeturn10search3turn23view0 |
| Scheduler simplicity | Simpler wake logic | Simpler “what to plan” semantics | Hybrid is still manageable if attempts are revision-scoped and cancellable. citeturn9view0turn23view0 |
| Preview quality | Better local relevance when cursor/dwell is meaningful | Better semantic quality because less biased by transient mouse motion | Use cursor as weak evidence, not as the planning target. citeturn23view0turn12search15 |
| Trust and acceptance | Safer by default | Stronger differentiation and “magic” when right | Publish silently only above threshold; back off after ignored/rejected previews. citeturn10search2turn10search1turn26academia8 |

**Final recommendation:** implement `AI_NEXT_ACTION_SCHEDULER` as a **semantic, attempt-based scheduler** with deterministic wake-up, proactive hidden planning, hard supersession on semantic commits/tool changes, and conservative publication rules. For routing, default the preview to a **short route-to-anchor overlay** rather than a full route-to-destination. For placement, default to a **landing + orientation recommendation**, but delay full ghost geometry until the user’s motion stabilizes enough that the preview will read as help rather than competition. citeturn23view0turn13view0turn13view2

## What the sources imply for KiSurf

The most important repo-level fact is that KiSurf already frames Next Action as a **semantic event episode**, not as raw cursor prediction. The public repo states that raw editor events are coalesced into semantic events; each Next Action episode runs through decision, hidden attempt, render/validation, LLM review, and publish/abandon; previews are preview-first and accept-gated; published previews carry provenance, preview lease, and accept-token metadata; active-routing previews can target a semantic anchor with `tool.routing.start` as fallback; and active placement contexts expose deterministic named candidate anchors rather than requiring pixel guessing. That architecture already leans strongly toward “proactive plan from semantic state, not from guessing mouse destination.” citeturn23view0

KiCad’s own router semantics also support this framing. After the interactive router starts, clicking a pad/track/via sets the track net from the starting item; moving the mouse then defines shape; clicking empty space **fixes the current routed segments and continues routing**, while clicking a same-net item finishes routing. KiCad also exposes concrete routing actions that matter to scheduling: **Finish Track**, **Undo Last Segment**, **Place Through Via / Microvia / Blind-Buried Via at the end of the currently routed track**, **Switch Track Posture**, **Track Corner Mode**, and layer-selection actions for via placement. Those are clear semantic boundaries around a known route head. citeturn13view0turn13view3

Placement has similar structure. KiCad’s placement and editing model already has stable semantic changes beyond raw cursor motion: footprints can be dragged subject to DRC-aware commit rules, flipped to the opposite side, rotated, positioned exactly, positioned relative to a reference, or positioned interactively using a reference vector. That means a placement scheduler can key off object identity, side, orientation basis, connected nets, anchor candidates, and conflict state rather than treating mouse travel as the main signal. citeturn13view1turn13view2

The HCI literature points to the same compromise. Research on proactive assistants consistently finds that being proactive is useful, but **bad timing** and **intrusive delivery** reduce trust and increase dismissal. A recent field study of proactive AI in IDE workflows found that interventions at workflow boundaries had materially higher engagement than those in the middle of work, and that well-timed proactive suggestions took less interpretation time than reactive ones. Other work on proactive support found that **aligned/adaptive timing** outperformed misaligned or random timing, improving both task outcomes and perceived dependability/benevolence. citeturn26academia8turn10search2

That evidence does **not** argue for “wait until stable before thinking.” Instead, it argues for **thinking early and publishing late**. A separate proactive-agent paper makes the architectural point explicitly: the always-on path should consume **structured event streams** and use lightweight wake/suppress logic, rather than calling an LLM on every low-level event. That maps well onto KiSurf’s own distinction between deterministic scheduler infrastructure and model-mediated decision-making. citeturn9view0turn23view0

## Strategy comparison in practical terms

### User experience

If the user is routing or placing quickly, pure Strategy A will usually feel calmer because it suppresses previews during fast motion. But that calmness comes at a cost: it delays computation until after motion settles, which means the suggestion often arrives **after** the useful moment has passed. Pure Strategy B handles latency much better because it starts the hidden attempt as soon as the semantic state becomes known. With 200–800 ms planning times, that overlap matters. Recent latency work on human-LLM interaction suggests latency is not just a scalar to minimize, but a design parameter; short delays can sometimes feel “less thoughtful,” while longer delays can become frustrating or reduce reliability. For KiSurf, the best reading is: do **not** put planning on the visible critical path if you can overlap it with the user’s movement. citeturn10search3turn23view0

For a “Tab-to-accept next action” workflow, Strategy B is also the better conceptual fit. Inline assistant paradigms in IDEs already normalize lightweight accept/reject controls such as **Tab to accept** and **Esc to dismiss**, and modern inline systems can even suggest edits away from the immediate cursor location rather than merely completing the current prefix. That pattern is much closer to KiSurf’s desired experience than a dwell-driven assistant that waits to infer cursor intent. citeturn11search1turn11search6turn12search15

### AI-native product fit

On product identity, Strategy B is clearly stronger. KiSurf’s README explicitly positions the system against thin plugin/chatbot patterns and argues for an AI-first collaborator that continuously proposes the next useful action in the editor itself. A planner that asks “given the current route head or placement object, what is the next semantically good landing?” is much more aligned with that vision than a system that mostly wakes after cursor dwell or post-motion stability. citeturn23view0

That said, Strategy A contributes a crucial lesson: **publish only when the suggestion can land socially and visually**. Research on unobtrusive proactive agents shows that context-adaptive, minimally intrusive delivery is preferred to blunt, explicit interruption. So Strategy A should survive not as the planner, but as the **publisher**. citeturn10search1turn10search13turn0search3

### Technical feasibility

For routing, Strategy B is technically feasible **if the planning target is an anchor or short semantic landing**, not a prediction of the exact click point. KiCad’s router already establishes the starting net from the clicked item and maintains a meaningful current routed endpoint with actions defined relative to “the end of the currently routed track.” KiSurf’s own current branch says route-to-anchor previews can target a semantic anchor directly, and can fall back to `tool.routing.start` when the model omits the start anchor. That is direct evidence that a route head plus candidate anchors is already a valid planning substrate. citeturn13view0turn13view3turn23view0

For placement, Strategy B is also feasible, but the planner needs stronger object context than routing does. KiSurf already exposes deterministic placement candidate anchors and visual anchor highlighting in active placement contexts, which means the model can reason over named anchors rather than freehand cursor forecasting. In addition, KiCad’s placement operations expose semantically important transforms such as rotate, flip, exact move, relative move, and interactive positioning. Together, those make “propose a landing/orientation/local correction” a realistic proactive task. citeturn23view0turn13view2

### Scheduling complexity

Pure Strategy A has simpler wake logic, but hybrid semantics are still tractable if every hidden attempt is keyed to a **context identity**. KiSurf’s own preview lease and accept-token structure is exactly the right foundation. Each attempt should be bound to at least: editor revision, tool family, semantic head/object identity, transform basis, and viewport generation. If any of those invalidate, the attempt is canceled or downgraded to stale. This is the same general architectural move recommended by proactive-agent triggering work and by code-completion “control model” work that separates inference triggering and suggestion filtering from the large model itself. citeturn23view0turn9view0turn12search0

## Recommended scheduler architecture

### Recommended state machine

The scheduler should be **deterministic on entry and cancellation**, **model-mediated on proposal choice**, and **confidence/stability-aware on publication**. A practical state machine is:

```text
IDLE
  -> ACTIVE_CONTEXT_KNOWN
     when routing_active with known route head
     or placement_active with known attached object

ACTIVE_CONTEXT_KNOWN
  -> HIDDEN_PLANNING
     spawn attempt_id with snapshot:
     {revision, tool_state, semantic_head/object, transform_basis, viewport_gen}

HIDDEN_PLANNING
  -> PUBLISH_ELIGIBLE
     if candidate completes, validates, and LLM review says publishable
  -> SUPERSEDED
     if commit/undo/tool-switch/object-change/layer-change/etc.
  -> IDLE
     if tool exits/cancels

PUBLISH_ELIGIBLE
  -> PREVIEW_VISIBLE
     if still relevant, lease valid, confidence above threshold,
     and current interaction tempo will not make preview adversarial
  -> HIDDEN_PLANNING
     if weak context changed enough to merit regeneration
  -> SUPERSEDED
     if semantic head/object changed first

PREVIEW_VISIBLE
  -> MATERIALIZE
     on explicit Accept
  -> IDLE
     on tool exit/cancel
  -> HIDDEN_PLANNING
     on dismiss/ignore timeout/backoff
  -> SUPERSEDED
     on any invalidating semantic event

MATERIALIZE
  -> ACTIVE_CONTEXT_KNOWN
     if tool remains active and new head/object state exists
  -> IDLE
     otherwise

SUPERSEDED
  -> ACTIVE_CONTEXT_KNOWN
     immediately restart from newest semantic state
```

This is the correct place to combine the two strategies. **Entry is proactive**, because latency is best hidden behind live motion. **Publication is stability-aware**, because user receptivity is higher at semantic or micro-workflow boundaries than in the middle of a committed hand movement. The always-on path should stay lightweight and structured, reserving the heavier model for actual candidate generation/review. citeturn23view0turn26academia8turn10search2turn9view0turn12search0

### Cancellation and supersession rules

The rule should be simple: **manual semantic progress always beats unpublished AI progress**. If the user commits before the preview publishes, the in-flight attempt is canceled immediately and a new attempt starts from the new state. KiSurf’s current provenance/lease/token model already points in this direction, because publication and acceptance are tied to context validity rather than to free-floating suggestions. citeturn23view0

In practice, an attempt should be canceled when any of the following change the semantic planning basis: editor revision, active tool family, route head or placement object identity, object transform basis, active layer/side when that affects legality, or candidate-anchor set version. Cursor movement alone should **not** cancel an attempt unless the mouse is being used as a weak ranking signal and the score changes materially; otherwise it should only update observational context. That design follows directly from the proactive-agent literature’s distinction between structured event triggers and noisy always-on low-level input. citeturn9view0

### Preview publication rules

A draft preview should become visible only when all of the following are true:

- the candidate still matches the current semantic head/object;
- validation passed;
- LLM review approves publication;
- preview lease is still valid;
- the user has not already progressed to a newer semantic state;
- the interaction tempo suggests the preview will read as assistance rather than competition. citeturn23view0turn10search1turn26academia8

For ignored or rejected previews, the scheduler should apply **local backoff** to similar contexts. Aligned timing improves user success and trust metrics, while misaligned timing harms them. So repeated ignores should not merely be logged; they should tune per-user and per-context publication thresholds, especially for motion-sensitive states like active routing. citeturn10search2turn26academia8

## Routing lifecycle and preview design

### Lifecycle

For routing, the best unit of work is the **current route head**, not the mouse path. When routing starts and the start anchor/net are known, spawn a proactive hidden attempt immediately. The planner should consider current net, current committed endpoint, active layer and allowed layers, netclass widths/vias, local obstacles, candidate anchors, and remaining ratsnest/airwire structure. KiCad’s router semantics and KiSurf’s route-to-anchor support already make this viable. citeturn13view0turn13view3turn23view0turn1search9

Planning should restart after every user-committed corner or fixed click. KiCad’s router explicitly treats clicking empty space as “fix current segments and continue routing,” so each such commit is the natural boundary for a new Next Action episode. This is also where the hybrid shines: the next hidden attempt can begin immediately after that commit, while visible publication waits until it will not fight the next hand motion. citeturn13view0turn26academia8

The current proactive attempt should be canceled on: user commits a segment/corner, **Undo Last Segment**, layer switch, start or cancellation of via placement, switch of track posture/corner mode if geometry basis materially changes, finish track, route cancel, or tool switch. KiCad exposes all of those as concrete routing actions or terminal conditions, so they are the right scheduler invalidation points. citeturn13view3turn23view0

### What to plan and what to show

The best default routing artifact is **a short route-to-anchor overlay**: more informative than a bare marker, less committal and less latency-heavy than a full route-to-destination attempt. Conveniently, KiSurf already supports active-routing route-to-anchor previews and lightweight anchor-focus markers. citeturn23view0

I would rank routing outputs this way:

1. **Primary:** a short route-to-anchor preview, possibly including the next corner and landing anchor.
2. **Secondary:** a ranked small set of candidate anchors, if confidence between top options is close.
3. **Occasional:** full route-to-destination attempts only when the destination is clearly implied, legality is strong, and the overlay will not visually dominate the workspace. citeturn23view0turn13view0

For acceptance semantics, “Accept” should usually mean **commit the proposed short route segment to the next anchor**, not “autoroute the rest of the net.” That aligns with KiCad’s existing continue-routing semantics and keeps the AI action granular, inspectable, and easy to supersede. A full-route accept can remain an advanced mode for especially obvious or low-risk cases. citeturn13view0turn23view0

## Placement lifecycle and preview design

### Lifecycle

For placement, the semantic unit is the **attached object plus its transform basis**. Once a footprint or other placeable object is attached to the cursor, the scheduler should launch a hidden attempt immediately using object identity, side/layer, rotation, connected nets, local conflicts, mechanical boundaries, keepouts, and candidate anchors. KiSurf’s current branch already surfaces deterministic placement candidate anchors and highlights them visually for the model, which is exactly the right substrate for proactive next-landing planning. citeturn23view0

Planning should restart after every committed placement **if the tool remains armed**. Cancel or supersede the current attempt on commit, cancel, tool switch, rotate, flip, side/layer change, selected-item change, or any edit that changes the relevant anchor/conflict field. KiCad’s placement operations explicitly define rotate/flip/relative positioning semantics, and KiSurf requires preview validity to remain tied to tool state and revision. citeturn13view2turn23view0

### What to plan and what to show

Placement differs from routing in one important way: a placement suggestion often has multiple coupled outputs—**landing point, orientation, side, and sometimes a local nudge**. So the internal planner should consider the whole tuple, but the visible preview can be progressive:

- when motion is high, show only the **anchor marker** or named landing cue;
- when motion stabilizes, upgrade to a **ghosted placement** with orientation/flip;
- keep all geometry-changing materialization behind explicit Accept. citeturn23view0turn10search1

That publication strategy is important because placement previews can feel more visually “competitive” than route hints. Unobtrusive-assistant research consistently favors context-adaptive, minimally intrusive delivery. For KiSurf, that means hidden planning should be proactive, but **visible placement geometry should be the more conservative part of the system**. citeturn10search1turn0search3

Accept for placement should usually mean **apply the complete local placement action**—position plus orientation and side if part of the same validated candidate—because partial acceptance is less natural in placement than in routing. If confidence is lower, the preview can degrade to an anchor-only or nudge-only suggestion. citeturn23view0turn13view2

## Auto-fill, instrumentation, risks, and implementation direction

### Auto-fill and refill

Your hypothesis is basically correct: **auto-fill/refill should remain event-driven rather than mouse-motion-driven or arbitrary-typing-driven**. KiSurf already treats routing, placement, and auto-fill/refill as work states inside one runtime, but the strongest architecture is to trigger fill/refill work from **semantic commit events**: property dialog commits, accepted Next Action edits, batch placement/routing commits, paste completion, range selection stabilization, and explicit rule/context changes. That matches both the proactive-agent view that the always-on path should consume structured events rather than noisy raw streams, and KiCad’s own handling of zone refill as an editing/response operation tied to meaningful edit stages rather than to every transient interaction. citeturn23view0turn9view0turn27search3turn27search0

### Required KiCad and KiSurf instrumentation

The scheduler should rely on deterministic instrumentation, not on the LLM inferring low-level UI state. The most important instrumented fields are:

- **Global scheduler identity:** editor revision, semantic event id, attempt id, preview lease id, accept token, viewport generation, selection generation. citeturn23view0
- **Routing context:** `routing_active`, active net, start anchor, current committed route endpoint, current layer and allowed layers, netclass width/via defaults, via mode, posture/corner mode, candidate anchors, remaining airwire/ratsnest cues, local obstacle summary. citeturn23view0turn13view0turn13view3turn1search9
- **Placement context:** `placement_active`, attached object id/type, side/layer, rotation, transform basis, candidate anchors, local conflict map, connected nets, board outline/keepouts, symmetry/repetition hints if derivable. citeturn23view0turn13view2
- **Interaction tempo:** cursor velocity, dwell estimate, recent manual-commit cadence, recent ignore/reject history. These should influence **publication**, not define the target of planning. citeturn26academia8turn10search2

### Risks and mitigations

The main risk is **preview competition**: the AI shows something correct but badly timed. The mitigation is to let proactive planning run in the background while requiring a publication gate that checks tempo, relevance, and lease validity. Evidence from proactive IDE and assistant work strongly supports that timing discipline. citeturn26academia8turn10search1

The second risk is **thrash and wasted compute**. The mitigation is a two-tier design: lightweight deterministic event coalescing and wake/suppress logic on the always-on path, with the heavier model used only for actual candidate generation/review. This is also consistent with emerging work on proactive-agent triggers and with production code-completion work that uses auxiliary control models to decide when to trigger and filter outputs. citeturn9view0turn12search0

The third risk is **stale or low-trust suggestions**. The mitigation is hard binding to revision/tool/object identity, silent publication only above threshold, and per-context backoff after ignored or rejected previews. Aligned proactive timing improves dependability/benevolence perceptions; misalignment does the opposite. citeturn23view0turn10search2

### Concise implementation direction for `AI_NEXT_ACTION_SCHEDULER`

Implement `AI_NEXT_ACTION_SCHEDULER` as five cooperating layers:

**Semantic wake layer.** Coalesce raw events into semantic events such as `route_started`, `route_corner_committed`, `route_head_changed`, `placement_object_attached`, `placement_transform_changed`, `placement_committed`, `zone_edit_committed`. This layer is deterministic and cheap. citeturn23view0turn9view0

**Attempt manager.** On `routing_active` or `placement_active`, spawn a proactive hidden attempt immediately once the semantic head/object is known. Bind it to revision, tool state, object/head identity, transform basis, and viewport generation. Cancel on any superseding semantic event. citeturn23view0

**Planner interface.** For routing, ask for a **next landing anchor or short route-to-anchor**. For placement, ask for a **landing/orientation/local correction tuple**. Mouse position is optional ranking context, not the planning target. citeturn23view0turn12search15

**Publisher.** Publish only if validation passes, review approves, lease is still valid, and interaction tempo is compatible with a visible preview. Prefer marker-first publication for lower-confidence placement/routing moments, and richer ghost overlays only when they will not visually fight the user. citeturn23view0turn10search1turn26academia8

**Accept-gated materializer.** `Tab`-style accept should apply the reviewed edit only after revalidating token, lease, and context freshness. This matches KiSurf’s current preview-first, token-gated direction and aligns with established inline-accept interaction patterns. citeturn23view0turn11search1turn11search6

Taken together, that yields the clearest answer to your research brief:

**Do not choose between “wait for stability” and “plan proactively.”**  
Use **deterministic active-state detection**, **immediate proactive hidden planning**, and **stability-aware publication**. For KiSurf, that hybrid is not a compromise in the weak sense; it is the design that most fully matches the repo’s stated AI-native direction, KiCad’s router/placement semantics, and the best available evidence on proactive assistant timing, trust, and latency handling. citeturn23view0turn13view0turn13view2turn26academia8turn10search2turn10search3