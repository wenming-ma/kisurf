# AI Agent Panel Context Injection Design

Date: 2026-06-19

## Purpose

The Agent pane can now produce a semantic `AI_PANEL_STATE_RECORD`, and
`kisurf_get_workspace_view` can carry that record when it is present in
`AI_CONTEXT_SNAPSHOT::m_PanelStates`. The live Agent request path still needs
to insert this record automatically.

This spec defines a narrow runtime wiring step: before the Agent sends a user
message to the provider, or before the background Agent reacts to editor
activity, the panel merges its current semantic state into the context
snapshot.

## Source Observations

- `AI_AGENT_PANEL::SendCurrentText()` obtains a context snapshot from
  `m_ContextProvider`, sets the editor kind fallback, and passes the snapshot
  to `AI_AGENT_PANEL_MODEL::SendUserText()`.
- `AI_AGENT_PANEL::RecordActivity()` obtains a context snapshot from
  `m_ContextProvider`, uses it to save workspace context state, expires suggestions, and
  calls `UpdateSuggestionsIfBackgroundEnabled()`.
- `AI_AGENT_PANEL::SemanticPanelStateRecord()` now returns the live Agent pane
  semantic state as an `AI_PANEL_STATE_RECORD`.
- `AI_CONTEXT_SNAPSHOT::m_PanelStates` is a vector, so repeated injection
  should replace the existing `agent.panel` record instead of appending
  duplicates.

## Goals

1. Add a pure helper that upserts an `AI_PANEL_STATE_RECORD` into an
   `AI_CONTEXT_SNAPSHOT` by `m_Id`.
2. Use that helper from `AI_AGENT_PANEL` before provider/model calls that use
   context snapshots.
3. Preserve existing editor-context provider behavior and editor-kind fallback.
4. Ensure empty panel records are ignored.
5. Keep the implementation testable without constructing wx windows where
   possible.

## Non-Goals

- No provider API changes.
- No new semantic tool names.
- No visual capture changes.
- No editor-level event recorder changes.
- No IPC endpoint or remote-control server.
- No changes to action execution policy.

## Runtime Behavior

`AI_AGENT_PANEL` should centralize snapshot preparation:

```cpp
AI_CONTEXT_SNAPSHOT AI_AGENT_PANEL::contextSnapshotWithPanelState() const;
```

The helper should:

1. Start with `m_ContextProvider()` if available, otherwise an empty snapshot.
2. Fill `m_EditorKind` from `m_EditorKind` when the provider left it unknown.
3. Upsert `SemanticPanelStateRecord()` into `m_PanelStates`.
4. Return the enriched snapshot.

`SendCurrentText()` should use the enriched snapshot before clearing the input,
so the semantic tree can reflect that the input had sendable text without
exposing the text value.

`RecordActivity()` should use the enriched snapshot before saving workspace context state
and before background suggestion generation.

## Pure Helper

Add to `include/kisurf/ai/ai_agent_panel_semantic.h`:

```cpp
KICOMMON_API void AiUpsertPanelStateRecord(
        AI_CONTEXT_SNAPSHOT& aSnapshot,
        AI_PANEL_STATE_RECORD aRecord );
```

Behavior:

- If `aRecord.HasState()` is false, do nothing.
- If another record has the same non-empty `m_Id`, replace it.
- Otherwise append the record.

## Testing Requirements

Common tests must cover:

1. Upsert appends a first valid panel record.
2. Upsert replaces an existing record with the same ID.
3. Upsert ignores an empty record.
4. `AI_AGENT_PANEL` exposes a private preparation method only through behavior,
   not as a public API. Since wx construction is outside the common pure tests,
   compile-time surface should remain focused on public methods.
5. The implementation should keep existing Agent panel semantic and workspace
   view tests passing.

## Acceptance Criteria

- User-send context snapshots and background-activity snapshots are enriched
  with the current Agent panel semantic state.
- Duplicate `agent.panel` records are avoided.
- Pure upsert tests pass.
- Existing Agent panel semantic, Agent panel, and semantic tool handler tests
  pass.
- `pcbnew` and `eeschema` still build.
- `git diff --check` and a secret scan pass.

## Self-Check

- Scope check: This wires existing state into existing snapshots; it does not
  expand model capabilities or editor control surfaces.
- Safety check: The injected record uses the existing redacted semantic panel
  projection.
- Architecture check: The model continues to read through the single context
  and workspace-view path.
