# AI Agent Panel Semantic Log Summary Design

Date: 2026-06-19

## Purpose

The Agent pane semantic tree currently exposes the Log tab as a text node with
only a count, such as `4 entries`. That proves the panel has logs, but it does
not let the model or tests inspect what the engineer can see in the Log tab.

This spec adds a bounded, redacted recent-log summary to the Agent panel
semantic tree. The summary flows through the existing
`AI_PANEL_STATE_RECORD` projection and therefore through
`kisurf_get_workspace_view` when panel state is included.

## Source Observations

- `AI_AGENT_PANEL_SEMANTIC_VIEW` currently carries counts for messages,
  suggestions, and log entries.
- `AiAgentPanelSemanticTree()` maps `m_LogEntryCount` to the
  `agent.log.entries` node text value.
- `AI_AGENT_PANEL::SemanticUiTree()` can already access
  `m_Model->ObservabilityEntries(128)`.
- `RedactSemanticUiText()` is already applied when semantic text nodes are
  added.

## Goals

1. Add an optional log summary field to `AI_AGENT_PANEL_SEMANTIC_VIEW`.
2. Keep default behavior unchanged: when no summary is provided, the log node
   still says `N entries`.
3. Make the `agent.log.entries` node expose the summary as safe plain text
   when provided.
4. Build the live Agent panel summary from recent observability entries.
5. Bound the live summary to a small number of recent entries and redact it
   before it enters the semantic tree.

## Non-Goals

- No UI layout redesign.
- No new Agent panel widgets.
- No provider schema changes.
- No changes to observability entry generation.
- No full raw details JSON in the semantic log node.

## Semantic View Change

Add to `AI_AGENT_PANEL_SEMANTIC_VIEW`:

```cpp
wxString m_LogSummary;
```

`AiAgentPanelSemanticTree()` should use:

- `m_LogSummary` when it is non-empty.
- `"%zu entries"` when `m_LogSummary` is empty.

## Live Summary Format

`AI_AGENT_PANEL::SemanticUiTree()` should summarize the most recent entries
from `m_Model->ObservabilityEntries(16)`.

The summary should contain at most 6 lines. Each line should use:

```text
#<sequence> <kind>: <title> - <summary>
```

If the entry has no summary, omit the ` - <summary>` suffix.

The final text must pass through `RedactSemanticUiText()` through the existing
semantic node path.

## Testing Requirements

Common tests must cover:

1. A custom `m_LogSummary` appears as the `agent.log.entries` text value.
2. The summary is redacted when it contains key-shaped or credential-shaped
   text.
3. Empty summary falls back to the old count text.
4. Existing panel-state projection tests still see the same default count text.
5. `AI_AGENT_PANEL` keeps exposing the same semantic API.

## Acceptance Criteria

- Agent panel semantic state includes recent log summary when available.
- Existing default count behavior remains compatible.
- Secret-shaped text is redacted.
- Targeted common tests pass.
- `pcbnew` and `eeschema` still build.
- `git diff --check` and secret scan pass.

## Self-Check

- Scope check: This only enriches semantic panel content; it does not change
  model behavior or UI layout.
- Safety check: Redaction is applied through the existing semantic text path.
- Architecture check: The data still flows through `AI_SEMANTIC_UI_TREE` and
  `AI_PANEL_STATE_RECORD`, not a new side channel.
