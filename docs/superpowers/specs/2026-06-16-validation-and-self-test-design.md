# Validation And Self Test Design

Date: 2026-06-16

## Purpose

This spec defines the validation and self-test strategy for KiSurf. It covers DRC/ERC-backed validation for AI suggestions and the staged approach for UI automation: try Computer Use first, then implement a native semantic-tree interface only if needed.

## Goals

- Validate accepted AI edits with KiCad-native DRC/ERC concepts.
- Record validation summaries in suggestions and trace.
- Use Computer Use as the first UI automation tool during early testing.
- Specify a native semantic-tree self-test interface for later implementation.
- Avoid spending early implementation effort on custom UI automation before the core AI workflow exists.

## Non-Goals

- No semantic-tree automation implementation in the first development slice.
- No replacement for KiCad's DRC/ERC engines.
- No requirement to automate every KiCad UI control before Agent and context foundations exist.
- No cloud-based test runner dependency.

## Validation Runner

### Responsibilities

`AI_VALIDATION_RUNNER` provides a single internal interface for AI validation.

Inputs:

- Active document context.
- Suggestion ID.
- Edit plan ID.
- Validation scope.
- Before/after marker snapshots where available.

Outputs:

- `AI_VALIDATION_SUMMARY`.
- Blocking status.
- Human-readable explanation.
- Links or references to raw DRC/ERC results where available.

### Validation Scopes

- `None`: chat-only suggestions.
- `LocalPreflight`: fast local checks before preview or apply.
- `PostApplyLocal`: checks directly affected objects after apply.
- `FullPcbDrc`: full PCB DRC through existing job/report pathways.
- `FullSchErc`: full schematic ERC through existing job/report pathways.
- `HeadlessBatch`: later CI/headless validation.

### Source Anchors

- `common/jobs/job_rc.h`
- `common/jobs/job_pcb_drc.h`
- `common/jobs/job_sch_erc.h`
- `pcbnew/pcbnew_jobs_handler.cpp`
- `eeschema/eeschema_jobs_handler.cpp`
- `pcbnew/drc/drc_report.cpp`
- `eeschema/erc/erc_report.cpp`
- `pcbnew/api/headless_pcb_context.cpp`
- `eeschema/api/headless_sch_context.cpp`

## Validation Policy

For the first implementation:

- Chat-only suggestions do not run DRC/ERC.
- Preview-only suggestions may run no validation or local preflight.
- Accepted PCB edits must run at least local post-apply validation when the edit session supports document mutation.
- Full DRC/ERC can be deferred until the validation runner skeleton is wired.

Blocking behavior:

- A validation result with new severe violations blocks apply when discovered before apply.
- A validation failure after apply reports clearly and leaves the edit undoable.
- Existing violations are not attributed to AI unless before/after diff shows they are new or worsened.

## Computer Use Trial

Before building native semantic-tree UI automation, KiSurf testing should try Codex Computer Use for:

- Opening KiCad or the relevant editor executable.
- Opening a sample project.
- Showing the Agent pane.
- Sending a simple message.
- Verifying a stub response appears.

The implementation plan should include a manual or semi-automated test checklist for this trial once the Agent pane exists.

## Deferred Semantic-Tree Interface

If Computer Use is insufficient, implement a native semantic-tree interface.

Required capabilities:

- Capture current running program screenshot.
- Enumerate visible semantic UI nodes.
- Include node bounds in screen coordinates.
- Include node role, label, enabled state, visible state, action ID when available, and text value when safe.
- Invoke a node action by stable node ID.
- Fill text into editable nodes by stable node ID.
- Click a coordinate or semantic node.

This interface should be native to KiCad and available only in debug/test builds or behind an explicit developer setting.

## Semantic Tree Data Model

Future node fields:

- `nodeId`
- `parentNodeId`
- `role`
- `label`
- `bounds`
- `enabled`
- `visible`
- `focused`
- `actionName`
- `toolActionId`
- `textValuePolicy`
- `children`

Screenshot result:

- Image bytes or file path.
- Capture timestamp.
- Top-level frame ID.
- Device scale factor.
- Coordinate space metadata.

Action invocation result:

- Success flag.
- Error code.
- Error message.
- New focused node ID when available.

## Safety And Privacy

- Do not expose password fields or secret values in semantic tree text.
- Do not enable semantic-tree automation by default in production builds.
- Require explicit developer setting or test mode.
- Tool calling must return structured errors instead of silently clicking.

## Testing Requirements

For validation runner:

- Unit test validation summary severity aggregation.
- Unit test before/after diff classifies existing violations separately from new violations.
- Unit test blocking policy for severe new violation.

For semantic-tree interface when implemented later:

- Unit test node serialization.
- Unit test coordinate conversion.
- Integration test enumerates Agent pane controls.
- Integration test clicks Send through semantic node action.
- Integration test captures screenshot metadata.

## Acceptance Criteria

- Validation runner skeleton can represent validation results before real DRC/ERC execution is wired.
- Suggestion records can include validation status.
- The first Agent pane self-test plan uses Computer Use first.
- Native semantic-tree automation is specified and deferred, preserving current implementation focus.
