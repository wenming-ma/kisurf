# KiSurf AI Native Spec Set

Date: 2026-06-16

This spec set defines the first implementation-ready slice for KiSurf's AI-native architecture. It translates the research direction into bounded specifications that must be reviewed before production code is written.

## Scope

KiSurf will be built as a native KiCad integration, not as a thin external chat plugin. The first implementation slice must create the architectural foundations for:

- A native AI runtime surface that can be used by schematic and PCB editors.
- A dedicated Agent window where users can chat with or command the assistant.
- A context layer that can observe editor state through native model/listener seams.
- A preview/materialize loop that separates non-persistent visual proposals from accepted, undoable editor changes.
- A validation and self-test strategy, including a deferred semantic-tree UI automation interface.

The semantic-tree automation interface is specified now but remains a later implementation step. The immediate development sequence should prioritize the AI runtime skeleton, Agent window, and native context/edit foundations.

## Spec Documents

1. [AI Native Foundation](./2026-06-16-ai-native-foundation-design.md)
   - Defines the shared native modules, data contracts, lifetime rules, and provider boundaries.

2. [Agent Window](./2026-06-16-agent-window-design.md)
   - Defines the dockable Agent UI, user interactions, and connection to the runtime.

3. [Context Preview Materialize](./2026-06-16-context-preview-materialize-design.md)
   - Defines editor observation, non-undo preview sessions, and accepted edit sessions.

4. [Validation And Self Test](./2026-06-16-validation-and-self-test-design.md)
   - Defines DRC/ERC-backed validation, Computer Use trial expectations, and the later semantic-tree automation interface.

5. [AI Context Tool Provider Phase 2](./2026-06-16-ai-context-tool-provider-phase2-design.md)
   - Defines model-facing context snapshots, action catalogs, provider configuration, and the OpenAI-compatible provider seam.

6. [AI Tool Execution Activity Trace](./2026-06-16-ai-tool-execution-activity-trace-design.md)
   - Defines safe model-requested tool invocation, allowlist policy, and user/model activity tracing.

7. [AI Runtime Tool Call Loop](./2026-06-16-ai-runtime-tool-call-loop-design.md)
   - Defines OpenAI-compatible tool-call parsing, runtime tool-call tracing, and an optional policy-gated handler boundary.

8. [AI Native Visual Snapshot](./2026-06-16-ai-native-visual-snapshot-design.md)
   - Defines current canvas capture into in-memory PNG data URIs attached to AI context snapshots.

9. [AI Editor Activity Timeline](./2026-06-16-ai-editor-activity-timeline-design.md)
   - Defines native tool-event observation, user/editor activity mapping, and recent activity context for model requests.

10. [AI Suggestion Preview Orchestrator](./2026-06-16-ai-suggestion-preview-orchestrator-design.md)
   - Defines the native lifecycle for pending suggestions, preview, accept, reject, and stale-context expiration.

11. [AI Agent Suggestion Bridge](./2026-06-16-ai-agent-suggestion-bridge-design.md)
   - Defines how Agent model/UI surfaces bounded suggestion records without claiming real editor mutation.

12. [AI Editor Object Resolution](./2026-06-16-ai-editor-object-resolution-design.md)
   - Defines read-only mapping from AI object references back to native PCB and schematic objects.

13. [AI Native Preview Adapter](./2026-06-16-ai-native-preview-adapter-design.md)
   - Defines editor-specific preview adapters that show resolved AI references through native view preview groups.

14. [AI Native Edit Adapter](./2026-06-16-ai-native-edit-adapter-design.md)
   - Defines commit-backed editor edit adapters that materialize accepted AI suggestions through native PCB and schematic undo pipelines.

15. [AI Action Tool Call Handler](./2026-06-16-ai-action-tool-call-handler-design.md)
   - Defines the common-layer bridge from parsed model tool calls to policy-gated action invocation requests.

16. [AI Editor Action Runner Integration](./2026-06-16-ai-editor-action-runner-integration-design.md)
   - Defines live PCB/schematic Agent pane installation of the action tool-call handler through a reusable `TOOL_MANAGER` runner.

17. [AI Agent Suggestion Review Controls](./2026-06-16-ai-agent-suggestion-review-controls-design.md)
   - Defines Agent pane Preview/Accept/Reject controls and editor callback wiring for current-state preview and bounded move edits.

18. [AI OpenAI Multimodal Visual Transport](./2026-06-16-ai-openai-multimodal-visual-transport-design.md)
   - Defines transport of native visual snapshots as OpenAI-compatible multimodal image content.

19. [AI Provider Check Action Tool](./2026-06-16-ai-provider-check-action-tool-design.md)
   - Defines OpenAI-compatible declaration of the dry-run `kisurf_check_action` tool.

20. [AI Runtime Tool Result Continuation](./2026-06-16-ai-runtime-tool-result-continuation-design.md)
   - Defines one bounded provider continuation turn after native tool handling.

21. [AI Read-Only Action Policy](./2026-06-16-ai-readonly-action-policy-design.md)
   - Defines policy-level auto-allow behavior for enabled read-only actions.

22. [AI Model Suggestion Provider](./2026-06-16-ai-model-suggestion-provider-design.md)
   - Defines grounded model JSON conversion into previewable suggestion records.

23. [AI Move Preview Adapter](./2026-06-16-ai-move-preview-adapter-design.md)
   - Defines clone-based move previews for accepted suggestion move arguments.

24. [AI Suggestion Operation Parser](./2026-06-16-ai-suggestion-operation-parser-design.md)
   - Defines a common typed parser for model-authored suggestion operation arguments.

25. [AI Structured Context JSON](./2026-06-16-ai-structured-context-json-design.md)
   - Defines bounded machine-readable context JSON sent alongside prompt text.

26. [AI Tool Result JSON](./2026-06-16-ai-tool-result-json-design.md)
   - Defines stable machine-readable result envelopes for native tool execution.

27. [AI Runtime Missing Handler Result](./2026-06-16-ai-runtime-missing-handler-result-design.md)
   - Defines explicit denied tool results when no runtime tool handler is installed.

28. [AI Action Tool Denial JSON](./2026-06-16-ai-action-tool-denial-json-design.md)
   - Defines stable result JSON for action tool-call handler denials.

29. [AI Action Catalog Stable Order](./2026-06-16-ai-action-catalog-stable-order-design.md)
   - Defines deterministic, safety-prioritized ordering for model-visible action catalogs.

30. [AI Activity Sequence Propagation](./2026-06-16-ai-activity-sequence-propagation-design.md)
   - Defines propagation of logged activity sequence numbers into suggestion triggers.

31. [AI Runtime Activity Context Merge](./2026-06-16-ai-runtime-activity-context-merge-design.md)
   - Defines inclusion of prior runtime tool activity in future Agent model context.

32. [AI Context Object Stable Order](./2026-06-16-ai-context-object-stable-order-design.md)
   - Defines deterministic ordering for visible and selected context object lists.

33. [AI Unified Activity Timeline](./2026-06-16-ai-unified-activity-timeline-design.md)
   - Defines one ordered Agent activity timeline for user actions and runtime tool records.

34. [AI PCB Footprint Context Coverage](./2026-06-16-ai-pcb-footprint-context-coverage-design.md)
   - Defines PCB footprint object refs for component-level model context and resolution.

35. [AI PCB Routing Context Coverage](./2026-06-16-ai-pcb-routing-context-coverage-design.md)
   - Defines PCB track and via object refs for routing-level model context and resolution.

36. [AI PCB Arc Context Coverage](./2026-06-16-ai-pcb-arc-context-coverage-design.md)
   - Defines PCB arc routing refs for curved-route model context and resolution.

37. [AI Structured Object Details](./2026-06-16-ai-structured-object-details-design.md)
   - Defines optional object-ref details for geometry and engineering metadata.

38. [AI Schematic Structured Object Details](./2026-06-16-ai-sch-structured-object-details-design.md)
   - Defines schematic object-ref details for symbols, wires, buses, and labels.

39. [AI PCB Component And Pad Details](./2026-06-16-ai-pcb-component-pad-details-design.md)
   - Defines PCB footprint and pad details for component placement and routing context.

40. [AI PCB Drawing Shape Context](./2026-06-16-ai-pcb-drawing-shape-context-design.md)
   - Defines board-level drawing shape refs for board-edge and graphics context.

41. [AI PCB Zone And Keepout Context](./2026-06-16-ai-pcb-zone-keepout-context-design.md)
   - Defines board-level copper zone, rule-area, and keepout refs for placement and routing constraint context.

42. [AI PCB Text Context](./2026-06-16-ai-pcb-text-context-design.md)
   - Defines board-level text and textbox refs for user-visible PCB annotation context.

43. [AI PCB Footprint Child Context](./2026-06-16-ai-pcb-footprint-child-context-design.md)
   - Defines footprint-owned field, text, textbox, and graphic refs for component-local annotation and silkscreen context.

44. [AI PCB Fabrication Annotation Context](./2026-06-16-ai-pcb-fabrication-annotation-context-design.md)
   - Defines board-level target, barcode, table, table-cell, and dimension refs for fabrication and mechanical annotation context.

45. [AI Agent Entry Discoverability](./2026-06-17-ai-agent-entry-discoverability-design.md)
   - Defines the top-level `AI > Agent` menu contract for PCB and schematic editors.

46. [AI Tool State And Next Action Preview](./2026-06-17-ai-tool-state-next-action-preview-design.md)
   - Defines active tool-state context, background workspace context state, deterministic routing/via previews, and typed semantic command execution.

47. [AI Agent Observability Log](./2026-06-18-ai-agent-observability-log-design.md)
   - Defines the derived Agent log surface for model input, tool calls, tool results, model output, and background suggestion lifecycle events.

48. [AI Unified Context Anchors And Panel State](./2026-06-18-ai-unified-context-anchors-panel-state-design.md)
   - Defines semantic anchors and live panel-state records as first-class fields in the shared Agent context snapshot.

49. [AI PCB Semantic Anchor Generation](./2026-06-18-ai-pcb-semantic-anchor-generation-design.md)
   - Defines PCB context adapter generation of semantic anchors from real pad, via, route, shape, and footprint geometry.

50. [AI Anchor Route Preview Tool](./2026-06-19-ai-anchor-route-preview-tool-design.md)
   - Defines a semantic tool that resolves PCB anchor ids into a native route segment preview suggestion.

51. [AI Routing Tool-State Anchor Augmentation](./2026-06-19-ai-routing-tool-state-anchor-augmentation-design.md)
   - Defines transient PCB routing anchors derived from active tool state for model-selectable route landing points.

52. [AI Action Tool Preview Acceptance](./2026-06-19-ai-action-tool-preview-acceptance-design.md)
   - Defines forced dry-run action tool calls, action-preview suggestions, and explicit user Accept before native action execution.

## Architecture Position

The architecture is Native First, IPC Compatible.

Native modules own real-time editor collaboration: active tool state, selection, view snapshot, preview ownership, commit/undo integration, connectivity changes, and validation diffing. IPC and external MCP-style tools may later expose stable projections of these native modules, but they are not the source of truth for the first implementation.

## Implementation Order

The first implementation plan should follow this order:

1. AI native foundation module and testable data types.
2. Agent window shell integrated into PCB and schematic editors.
3. Context index listeners for PCB and schematic model changes.
4. Preview session skeleton using native view preview groups.
5. Edit session skeleton using `BOARD_COMMIT` and `SCH_COMMIT`.
6. Validation runner skeleton over existing DRC/ERC job/report concepts.
7. Computer Use test trial.
8. Deferred semantic-tree automation interface only if Computer Use is insufficient.
9. Phase 2 context snapshots, action catalog discovery, provider configuration, and offline-tested OpenAI-compatible provider seam.
10. Phase 3 tool execution gate, activity trace, and model/user action audit records.
11. Phase 4 provider/runtime tool-call loop that parses model tool calls and records or policy-gates them before any editor execution.
12. Phase 5 native visual snapshots that attach current canvas pixels to model context.
13. Phase 6 native editor activity timeline that records recent user actions and selection/move events into model context.
14. Phase 7 suggestion preview orchestration that turns sensed context and activity into bounded, reviewable suggestion candidates.
15. Phase 8 Agent suggestion bridge that makes suggestion records visible and model-controllable from the Agent pane.
16. Phase 9 editor object resolution that maps `AI_OBJECT_REF` values back to native PCB/SCH objects for later preview and commit adapters.
17. Phase 10 native preview adapters that show resolved AI references through `KIGFX::VIEW` preview groups without editor mutation.
18. Phase 11 native edit adapters that resolve AI object references, stage native commits, move items by a bounded delta, and revert on failed materialization.
19. Phase 12 action tool-call handler that resolves model `kisurf_run_action` requests against context action descriptors and routes them through the deny-by-default executor.
20. Phase 13 editor action runner integration that installs the handler into PCB and schematic Agent panes through a conservative `TOOL_MANAGER` runner and one-action allowlist.
21. Phase 14 Agent suggestion review controls that let users preview, accept, or reject the newest active suggestion through native editor callbacks.
22. Phase 15 OpenAI-compatible multimodal visual transport that sends native canvas data URIs as image content.
23. Phase 16 provider check-action declaration that lets models dry-run action policy before requesting execution.
24. Phase 17 runtime tool-result continuation that returns model-visible final text after native tool handling.
25. Phase 18 read-only action policy that lets models execute safe catalog actions without per-action allowlist plumbing.
26. Phase 19 model-backed suggestion provider that turns grounded JSON into previewable suggestions.
27. Phase 20 clone-based move preview adapters for PCB and schematic suggestions.
28. Phase 21 common suggestion operation parser for model-authored move arguments.
29. Phase 22 structured context JSON for provider-bound model requests.
30. Phase 23 stable tool result JSON envelopes for model continuation turns.
31. Phase 24 runtime no-handler tool results with activity audit coverage.
32. Phase 25 handler-level action tool denial result JSON.
33. Phase 26 stable, safety-prioritized action catalog ordering for bounded model context.
34. Phase 27 logged activity sequence propagation from Agent panel events into suggestions.
35. Phase 28 runtime tool activity merge into future Agent model context.
36. Phase 29 stable context object ordering for bounded model-visible snapshots.
37. Phase 30 unified Agent activity sequence ordering across user and runtime records.
38. Phase 31 PCB footprint context coverage for component-level sensing and preview.
39. Phase 32 PCB routing context coverage for track/via sensing and preview.
40. Phase 33 PCB arc context coverage for curved-route sensing and preview.
41. Phase 34 structured object details for route geometry and engineering metadata.
42. Phase 35 schematic structured object details for symbol and connectivity primitives.
43. Phase 36 PCB component and pad details for placement and routing context.
44. Phase 37 PCB drawing shape context for board-edge and graphics sensing.
45. Phase 38 PCB zone and keepout context for copper-pour, rule-area, and keepout sensing.
46. Phase 39 PCB text context for silkscreen, fabrication note, and annotation sensing.
47. Phase 40 PCB footprint child context for reference/value fields, component-local text, and footprint graphics sensing.
48. Phase 41 PCB fabrication annotation context for targets, barcodes, tables, table cells, and dimensions.
49. Phase 42 Agent entry discoverability with a top-level AI menu in PCB and schematic editors.
50. Phase 43 tool-state context, per-mode Agent state, Next Action Preview providers, and typed semantic command execution.
51. Phase 44 Agent observability log that renders model input, tool calls, tool results, model output, and background suggestion lifecycle from existing trace/activity sources.
52. Phase 45 unified context anchors and panel state that let chat, background Agent, and future IPC projections read the same semantic targets and live UI state.
53. Phase 46 PCB semantic anchor generation that populates the unified anchor channel from real board geometry.
54. Phase 47 anchor route preview tool that lets the model use current PCB semantic anchor ids to request a native route segment preview.
55. Phase 48 routing tool-state anchor augmentation that exposes active routing start, current end, orthogonal, and 45-degree preview candidates as semantic anchors.
56. Phase 49 action tool preview acceptance that prevents model-originated `kisurf_run_action` calls from executing until the user accepts the generated preview suggestion.

## Non-Goals For The First Implementation Slice

- No production model provider integration is required before the runtime and Agent window can operate with a deterministic local stub provider.
- No autonomous placement or routing algorithm is required in the first slice.
- No semantic-tree UI automation implementation is required before Computer Use is attempted.
- No cloud upload of project data is allowed by default.
- No accepted AI edit may bypass KiCad's native commit/undo mechanisms.

## Review Gates

Development may begin only after:

- All spec documents exist.
- The spec set passes placeholder, contradiction, scope, and ambiguity checks.
- The user reviews and approves the committed spec set.
- An implementation plan is written from the approved specs.
- Implementation follows test-first development for new behavior.

## Current Source Anchors

The specs intentionally anchor to existing KiCad seams:

- `common/api/api_handler_editor.cpp` for commit-style API lifecycle concepts.
- `pcbnew/board_commit.cpp` and `eeschema/sch_commit.cpp` for accepted edit materialization.
- `pcbnew/board.h`, `pcbnew/board.cpp`, `eeschema/schematic.h`, and `eeschema/schematic.cpp` for model listeners.
- `include/view/view.h` and `common/view/view.cpp` for preview ownership.
- `include/properties/property_mgr.h` and `include/dialog_shim.h` for field/property context.
- `common/jobs/job_rc.h`, `common/jobs/job_pcb_drc.h`, `common/jobs/job_sch_erc.h`, `pcbnew/pcbnew_jobs_handler.cpp`, and `eeschema/eeschema_jobs_handler.cpp` for validation concepts.
- `pcbnew/pcb_edit_frame.cpp`, `eeschema/sch_edit_frame.cpp`, and existing AUI pane patterns for Agent window integration.
