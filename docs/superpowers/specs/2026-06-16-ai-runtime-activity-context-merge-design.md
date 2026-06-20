# AI Runtime Activity Context Merge Design

Date: 2026-06-16

## Problem

`AI_RUNTIME` records model tool-call requests and tool results, but `AI_AGENT_PANEL_MODEL` only adds
the user/editor activity log to the next provider request context. A later model turn can therefore
see what the engineer did, but not what the model itself asked KiSurf to do in earlier turns.

## Goals

- Include prior runtime tool-call activity in future Agent model requests.
- Preserve existing user/editor activity inclusion.
- Keep runtime request ids, tool-call ids, and result JSON available to the model.

## Non-Goals

- No unified cross-log sequence allocator in this slice.
- No change to runtime tool execution policy.
- No change to activity log capacity.

## Design

`AI_AGENT_PANEL_MODEL::ActivityRecords` will return user/editor activity followed by runtime activity.
`SendUserText` will use this combined view when appending recent activity to the outgoing context
snapshot.

Runtime records keep their own sequence values and request ids. A later slice may introduce a global
activity timeline, but this slice only closes the model-context visibility gap.

## Verification

- Add unit coverage where one Agent request triggers a tool call and a later request verifies that
  the outgoing context contains the previous `ModelToolRequest` and `ToolResult` records.
- Re-run Agent panel model and runtime tests.

## Self Review

- This exposes only records already produced by native runtime handling.
- The context merge is read-only and does not make any tool action easier to execute.
- Existing callers of `ActivityRecords` get a more complete audit view.
