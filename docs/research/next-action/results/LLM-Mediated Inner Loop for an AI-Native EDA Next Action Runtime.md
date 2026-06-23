# LLM-Mediated Inner Loop for an AI-Native EDA Next Action Runtime

## Executive recommendation

The strongest long-term architecture for KiSurf is a **single, editor-native attempt runtime** shared by placement, routing, and structured auto-fill/refill, with the LLM acting as the semantic decision-maker **inside** the loop but **outside** the authority boundary for truth, validation, preview publication, and commit. In practice, that means the model should observe context, decide whether to open an attempt, use layered tools against a rollbackable shadow state, inspect structured observations and rendered previews, revise as needed, and only then request publication of a user-visible preview. A separate native runtime-governance layer should decide whether the preview is publishable and whether an accepted preview is commit-safe. This direction matches KiSurf’s own repository framing: the README defines Next Action as an internal observe → decide → tool-call/shadow-mutate → render/validate → review → publish/retry/rollback/abandon loop, with suggestions tied to editor revision, selection, tool state, and viewport so stale suggestions can expire, and with live-board mutation delayed until explicit user acceptance. citeturn32view3turn33view0turn27view0

On the specific unresolved question of where objective checks should live, the best answer is **the hybrid model, strengthened into an explicit two-gate architecture**. Deterministic checks should be available as callable tools during the inner loop so the model can use them to improve candidates, **and** they should also be run automatically as mandatory gates by the runtime before preview publication and again before final commit when the preview is accepted. Comparable systems converge on this pattern. Claude Code explicitly recommends giving the agent a check it can run so it can iterate on its own, but it also supports deterministic stop hooks that block completion until the check passes. Robotics governance work similarly argues that safety and recovery logic should not be left solely inside the agent loop; it should be externalized into a dedicated runtime layer that performs policy checking, execution monitoring, rollback, and human override. citeturn19view0turn15view0turn15view1

The practical implication for KiSurf is straightforward: **validators are not “optional utilities,” and they are not “the model’s responsibility.”** They are native subsystems that produce facts. The LLM can request them during candidate generation and self-review, but it cannot bypass them when asking to publish or commit. That separation is what preserves the AI-native interaction model without turning correctness into a prompt-engineering problem. citeturn29view1turn27view0turn32view1

## What KiSurf already commits you to

KiSurf’s repository already rules out the plugin-assistant framing and points toward a much deeper runtime integration. The README describes KiSurf as “an AI-native PCB editor built on KiCad,” explicitly rejecting the idea of bolting on a chatbot, wrapping KiCad with an external MCP server, or shipping a thin plugin with narrow visibility. It also states that placement, routing, and auto-fill/refill are work states inside one LLM-mediated runtime rather than three separate agents or provider chains. citeturn3view0turn24view0

The runtime spec sharpens that direction into a concrete authority model. It says Python or agent cells may be the authoring surface, but the durable boundary is the C++ KiSurf session core, which owns board truth, typed mutations, preview truth, validation, rollback, journal replay, and final accept. It also states that the model should see session-level tools and bounded observation tools rather than an ever-growing list of composite commands, and that higher-level helpers must lower into typed atomic operations that enter the journal. In other words, extensibility is supposed to happen by adding new helpers, generators, and validators over a stable session/journal/shadow/preview substrate, not by changing the core loop every time you add a new capability. citeturn33view0turn33view1

That is an important architectural advantage. The session spec already gives you the core pieces you need for a proper hidden-attempt loop: an `AI_EXECUTION_SESSION` that owns lifecycle, epoch, checkpoints, validation state, and preview state; an `AI_SESSION_JOURNAL` that records ordered typed operations and structured results; an `AI_SHADOW_BOARD` as semantic preview truth; and an `AI_PREVIEW_MANAGER` as visual truth. It also explicitly requires that accept be a core-owned promotion, not a Python-owned write, that preview never mutates the live board, and that accept be rejected when the base hash does not match. citeturn33view0turn32view1turn32view2

The spec is especially aligned with your research topic because it already defines the attempt loop in implementation terms. Each step is supposed to produce three synchronized observations: a structured diff, validation results and warnings, and a native preview frame or handle. Checkpoints store journal position, epoch, shadow-board snapshot or replay anchor, preview object mapping, and validation cache generation; rollback restores the shadow board and preview scene, stales post-checkpoint handles, truncates later journal records, and leaves the live board untouched. The validation ladder is also already layered: typed argument validation, semantic validation, geometry validation, incremental connectivity updates, selective zone refill plus DRC-lite, and full DRC on demand or before high-risk accept. citeturn27view0turn27view1

Just as importantly, KiSurf has already started to encode anti-staleness and anti-race semantics. The README says suggestions must be tied to editor revision, selection, tool state, and viewport. The runtime spec says accept verifies the live-board base hash, rejects stale accept by default, blocks accept when `accept_validation_sufficient=false`, and detects mid-session selection conflicts. It also rejects stale or mismatched Python worker outputs so cross-session or outdated cells cannot enter the shadow board or journal. Those are exactly the kinds of controls an always-on background Next Action system will need when the user is editing quickly. citeturn32view3turn32view1turn32view4turn33view1

## Lessons from comparable systems

Across agentic coding systems, the common pattern is not “let the model reason and hope.” It is **tool use plus explicit feedback plus bounded execution**. Anthropic’s agent loop documentation describes a repeated cycle where the model evaluates state, requests tool calls, receives tool results, and keeps iterating until it has no more tool calls to make. Their best-practices documentation makes the validator lesson explicit: give the agent a pass/fail check it can run, because otherwise “looks done” becomes the only stop signal. They further distinguish between ordinary iterative checking and deterministic stop hooks that block the turn from ending until a scripted check passes, and they recommend checkpoints so sessions can rewind after bad edits. citeturn12view0turn19view0turn11view1

OpenAI’s Codex product and sandboxing model point in the same direction from a different angle. Codex runs each task in an isolated environment, can read and edit files, run tests, linters, and type checkers, and was specifically trained to iteratively run tests until it receives a passing result. It is designed to provide terminal logs and test outputs as verifiable evidence, and it keeps agent action inside a bounded environment so the trust model depends on enforced limits, not just on the model’s intentions. That is highly analogous to KiSurf’s shadow-board/session-journal/preview split: the model can act autonomously inside a constrained state space, but publication and integration stay under native control. citeturn22view0turn9view1

Code-editor UX patterns also matter for your publish model. Copilot Edits is described as an iterative review workflow where users accept what works, discard what does not, run code to verify changes, and undo back to a previous working state. GitHub Copilot’s next edit suggestions similarly emphasize low-distraction previews, gutter hints, Tab-to-navigate and Tab-to-accept behavior, and a collapsed mode that reduces visual noise until the user engages. Those product choices are not PCB-specific, but they are directly relevant to an ambient Next Action system: the runtime should hide internal failed trials, publish only one compact inspected preview at a time, and optimize for user flow rather than maximal visible autonomy. citeturn31view2turn31view1

Robotics papers strengthen the case for separating agent cognition from enforcement. The SafeGate paper argues that LLM-controlled robot systems need deterministic validation gates before execution and continuous constraint monitoring during execution; it decomposes safety into a gate plus ongoing task contracts checked with symbolic methods. The Runtime Governance paper goes further by arguing that governance should be externalized into a dedicated runtime layer that performs policy checking, capability admission, execution monitoring, rollback handling, and human override, instead of burying those responsibilities inside the agent loop. Another robotics paper on dynamic plan repair uses the LLM as an evaluator inside a formal planning loop rather than as the sole generator of executable plans, which is directly relevant to letting the LLM steer EDA attempts without giving it sole authority over constraint satisfaction. citeturn15view1turn15view0turn14view2

CAD and EDA examples point to the same structure. IDDAG for programmatic CAD uses a closed loop with systematic diagnostic feedback based on syntactic, runtime, and geometric analysis, and reports that iterative refinement improves exact geometry. A FreeCAD-oriented LLM paper likewise describes script generation followed by execution and iterative refinement based on error feedback, especially as designs become more constrained. ToolCAD frames CAD generation as an interactive tool-using environment with hybrid feedback signals and an explicit modeling workflow built around reasoning, tool execution, and reflection. On the hardware side, DRCY is not an editing loop, but it is notable because it places agentic semantic review inside a CI/CD-style review boundary, with multi-agent decomposition and consensus to improve reliability before comments are surfaced to engineers. citeturn17view2turn18view1turn17view0turn18view0

The general lesson from all of these systems is consistent: **inner-loop iteration improves results only when grounded in external signals, and high-trust publication requires a runtime-owned review boundary.** Pure self-review without deterministic feedback is weaker, while deterministic gates without inner-loop access waste one of the main benefits of an agentic system. citeturn29view1turn19view0turn15view0turn17view2

## Recommended inner-loop and gate design

The recommended core design is a **single attempt state machine** used across placement, routing, and structured auto-fill/refill, with provider-specific logic plugged into a common attempt loop. KiSurf’s own materials already point there: the README and research folder describe one unified runtime in which those three work states share infrastructure, and the session spec already supplies the stable abstractions needed to keep the core loop unchanged while new tools are added. citeturn32view3turn24view0turn33view0

A good long-term state machine looks like this:

```text
Idle
  -> ObserveContext
  -> DecideAttemptOrSkip
  -> OpenAttempt
  -> BeginStep
  -> Act
  -> Observe
  -> Review
  -> Revise or Rollback
  -> Repeat within budget
  -> PreviewGate
  -> PublishPreview
  -> AwaitUserDecision
  -> AcceptGate
  -> CommitOneTransaction
  -> Closed
```

The crucial detail is that **Act / Observe / Review / Revise happens only inside shadow state**. Placement suggestions should not jump straight from model output to visible footprint previews. Routing suggestions should not go from route intent to published path without first running constraint checks, connectivity updates, and at least local render review. Structured fill/refill suggestions should not appear until the structured-surface validator has inspected the hidden result. KiSurf’s own spec supports this because `kisurf_run_cell` can create checkpoints, execute typed mutations, record query and render results, and automatically append validation and preview feedback if the model did not request them explicitly. citeturn33view1turn27view0

The best publish model is a **two-gate model** owned by native runtime code rather than by the LLM prompt.

**Preview gate.** Before any user-visible preview is shown, the runtime should automatically verify: the context signature is still current; the attempt has no hard validation failures at the configured preview grade; the preview is render-valid; the candidate remains semantically relevant to current user intent; and the attempt stayed within time/turn/tool budgets. This gate should run whether or not the model explicitly called validators, because a background Next Action system cannot rely on the model to remember every guard on every attempt. That is the same logic behind Claude Code’s stop hooks and the robotics governance argument for externalized runtime checking. citeturn19view0turn15view0turn15view1

**Accept gate.** When the user presses Accept, the runtime should perform a stricter, exact-state validation check before replaying to the live board. KiSurf’s spec is already close to this: the accept applier verifies live-board base hash, rejects accept when the latest explicit validation says `accept_validation_sufficient=false`, replays only inside one live `BOARD_COMMIT`, runs required pre-push validation, and aborts without half-mutating the board if staging fails. The spec also says that accept-grade validation for DRC should be exact for the preview state, and when a session contains unaccepted mutations it reconstructs a temporary preview board and runs native `DRC_ENGINE` there instead of pretending that live-board DRC is enough. citeturn32view1turn6view0

That leads to the clearest answer to your option comparison.

**Checks only as tools during self-review** is too weak. It gives the model useful feedback, but it leaves publication correctness contingent on the model’s vigilance. Comparable systems explicitly add deterministic stop conditions because models otherwise stop when work merely “looks done.” citeturn19view0turn29view1

**Checks only as an automatic gate before preview publication** is better for safety, but it is still suboptimal because it deprives the inner loop of the very feedback that makes iterative improvement work. CAD and code systems improve via execution feedback, diagnostics, and test results, not just via one final gate. citeturn17view2turn18view1turn19view0

**Hybrid checks as tools plus mandatory gates** is the best baseline. It preserves free iteration while preventing premature preview publication and unsafe accept. It also matches KiSurf’s validation ladder and accept semantics. citeturn27view0turn32view1

**A better refinement of the hybrid model** is to formalize validators inside an **external governance layer**. In this design the LLM still asks for validators, but the runtime owns validator policy, minimum publish grade, exact accept grade, stale-context checks, cancellation, and commit authority. That is the closest match to the strongest robotics governance architecture and the most future-proof choice for an AI-native editor. citeturn15view0turn15view1turn33view0

## State, checkpoints, and publication

The runtime should represent each hidden attempt as a first-class object with enough structure to survive retries, cancellation, stale invalidation, and post-hoc debugging. KiSurf’s session journal already points in the right direction because it requires every operation record to include session id, step id, operation kind, typed arguments, handles, warnings, validation summaries, structured results, and before/after epoch. The journal is explicitly named the canonical replay and audit artifact, while preview items and shadow objects are derived from it. citeturn33view0

A practical attempt object for KiSurf should contain at least these fields:

```text
Attempt
  attempt_id
  work_state
  context_signature
  session_id
  current_step_id
  attempt_status
  checkpoint_stack[]
  observation_frames[]
  validation_facts[]
  review_decisions[]
  preview_record
  publish_grade
  accept_grade
  budget_counters
  latency_stats
```

The **context signature** should combine the board content hash, selection revision, viewport or visible-region revision, tool-state hash, and any structured-surface revision relevant to panel or property-grid edits. KiSurf already requires suggestions to be tied to editor revision, selection, tool state, and viewport, and its runtime spec already makes selection revisions and base-hash mismatch explicit blockers for cell execution and accept. citeturn32view3turn32view1turn32view4

The **checkpoint** should not be thought of as a convenience feature. It is the backbone of multi-step hidden attempts. KiSurf already specifies that a checkpoint stores journal index, session epoch, handle watermark, shadow snapshot or replay anchor, preview object mapping, and validation cache generation, and that rollback restores shadow state and preview while leaving the live board untouched. That is exactly what lets the model do “try corridor A, observe warnings, roll back, try corridor B” without polluting either the user view or the real board. citeturn27view0turn27view1

The **validation fact** should be stored as immutable structured data, not just as a boolean or a fuzzy natural-language comment. KiSurf’s validation flow already serializes native DRC issue metadata, backend name, status, warnings, and whether the result is accept-sufficient. Its spec also records whether validation was against the live board or a reconstructed preview board, and whether the preview-state match is exact. That is the right pattern because it lets later review logic reason over facts such as “geometry passed but native preview-board DRC failed” rather than over vague success strings. citeturn33view1turn6view0

The **preview record** should exist in two visibility tiers: internal-attempt preview and user-facing preview. KiSurf’s preview subsystem already tracks provenance metadata and clears previews on rollback, reject, cancel, close, or accept. For an always-on Next Action system, internal attempts should remain hidden; the user should see only the final promoted preview. This matches both KiSurf’s preview-first design and modern next-edit systems that deliberately collapse or suppress suggestions until the user engages, to reduce distraction and preserve trust. citeturn27view0turn32view1turn31view1

Publication should therefore be modeled as a promotion from **Pending** to **PreviewPublished**, not as a side effect of rendering. I would recommend the following suggestion lifecycle: `Pending`, `PreviewPublished`, `Accepted`, `Rejected`, `Expired`, and `Superseded`. “Expired” should mean the context moved; “Rejected” should mean the user dismissed it; “Superseded” should mean a newer attempt replaced it. That distinction matters for telemetry and future learning, because an engineer panning away is not the same signal as an engineer saying “this was wrong.” KiSurf’s own repository already emphasizes stale-expiration tied to editor context, and its research notes recommend a shared accept/reject/expire/supersede state machine. citeturn32view3turn26view3turn26view4

## Failure modes and runtime controls

**Stale context and fast user actions.** This is the most important operational risk for an ambient Next Action feature. KiSurf’s current direction is strong here: suggestions are tied to revision and viewport context; accept checks the live-board base hash; changed selection can block both new cell execution and accept; and stale/cross-session worker outputs are rejected before they touch shadow state. I would extend that by making cancellation cheap and default: any context-signature drift should invalidate unpublished attempts immediately, and published previews should downgrade to expired rather than trying to rebase in place in the MVP. citeturn32view3turn32view1turn32view4turn33view1

**Invalid previews.** KiCad’s own documentation is a reminder that DRC can be wrong if the underlying state is not current: zone refill can be required before DRC, and if custom rule definitions have errors, custom rules are not applied at all. That means KiSurf should never treat a bare “validation passed” bit as enough. Every validation result should carry backend, scope, exactness, refill state, warnings, and rule-load status. Preview publication should require at least preview-grade exactness for the relevant checks, while accept should require accept-grade exactness for the preview-equivalent board state. citeturn21view0turn33view1turn6view0

**Model overconfidence.** The best defense is not a bigger system prompt; it is external feedback plus runtime-owned gating. CRITIC explicitly argues that tool-interactive validation and revision improve results and highlights the importance of external feedback. Claude Code similarly recommends a second-opinion review path such as a verification subagent or a deterministic stop hook. For KiSurf, that means the model may author and critique candidates, but publish and accept decisions should depend on validator facts and policy thresholds, not on the model’s own confidence score alone. citeturn29view1turn19view0

**Validation false positives and false negatives.** Native EDA checks are authoritative, but not infallible in the product sense. A false positive may annoy users; a false negative may let a bad preview slip through. The right runtime response is to preserve the full validation artifact, annotate the preview with issue geometry when available, and keep severity and policy separate. KiCad’s DRC system already distinguishes violation severities and allows some checks to be ignored, while KiSurf’s validation service records structured issue metadata and can project issue information back onto shadow items. That makes it possible to publish a preview with annotated soft warnings while still blocking accept on hard failures. citeturn21view0turn6view0

**Excessive tool loops and runaway latency.** Background suggestion systems fail if they feel expensive. Claude’s SDK exposes `max_turns` and budget caps explicitly because open-ended loops can run long; KiSurf’s worker already has timeouts, cancellation, and hard-kill fallback so failed cells do not enter the journal; and the current spec intentionally keeps the Background Preview Agent constrained and preview-only while the session core matures. The right policy is to keep the core runtime general, but set strict budgets per work state: routing can spend a bit more than placement, and structured fill/refill should favor fewer higher-quality attempts over many micro-retries. Runtime policy, not the model, should own those budgets. citeturn12view0turn33view1turn27view0

**Premature preview publication.** The runtime should never equate “the model has a candidate” with “the user should see a candidate.” Code-editor systems increasingly use compact previews, low-distraction hints, and explicit user engagement before big edits are rendered. KiSurf should follow the same rule: internal attempts can iterate freely, but only one preview at a time should be promoted, and only after the preview gate passes. That keeps the model flexible without turning the editor into a flickering stream of speculative geometry. citeturn31view1turn31view2turn24view0

**Tool-surface sprawl.** The long-term defense against architectural drift is the one KiSurf already documents: keep the model-facing surface centered on session control, observation, validation, preview, checkpoint, rollback, and accept; let higher-level helpers and future domain tools lower into atomic operations. That keeps the core loop stable even as you add new placement generators, routing planners, structured-surface editors, or manufacturability analyzers. citeturn33view0turn33view1

Putting all of this together, the implementation-oriented answer is:

- Keep **one common hidden-attempt runtime** for placement, routing, and auto-fill/refill.
- Let the LLM decide whether to attempt, which tools to call, and how to revise.
- Keep **truth, validation grade, preview promotion, stale handling, and commit authority** in native runtime code.
- Make deterministic checks available **both** as inner-loop tools **and** as automatic gates.
- Distinguish **preview-grade** validation from **accept-grade** validation.
- Publish only a **single, reviewable preview** tied to a current context signature.
- Commit only after **user Accept** and a final exact validation pass through one native transaction.

That architecture is the one most consistent with KiSurf’s own branch direction, with comparable CAD/EDA and robotics patterns, and with the trust model that an AI-native EDA editor will need if the LLM is truly participating in every semantic step. citeturn33view0turn27view0turn32view1turn15view0turn17view2turn19view0