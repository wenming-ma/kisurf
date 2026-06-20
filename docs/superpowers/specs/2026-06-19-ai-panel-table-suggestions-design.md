# AI Panel Table Suggestions Design

Date: 2026-06-19

## Goal

Add a conservative background-Agent suggestion path for focused UI panels with table-like semantic state.  When a user edits one populated cell and the same column contains empty cells, the Agent can propose a reviewable column-fill suggestion.

## Problem

The unified context now exposes `panel_states`, and `dynamic_context` can classify focused panel work.  However, the default next-action pipeline only creates PCB layout/routing previews plus generic model-backed object suggestions.  It does not yet consume panel semantic state, so workflows like filling a clearance-rule column cannot be suggested deterministically.

This slice should add a generic provider that reads a bounded table schema from `AI_PANEL_STATE_RECORD::m_StateJson`.  It should not attempt to click UI controls or apply panel edits yet.

## Requirements

1. Add `AI_PANEL_TABLE_NEXT_ACTION_PROVIDER`.
2. Register it in the default `AI_NEXT_ACTION_CONTROLLER`.
3. The provider only suggests when `AiDynamicContextKind(snapshot) == "panel"`.
4. The provider scans panel states with focused controls and parseable table state.
5. Supported panel state shape:

```json
{
  "tables": [
    {
      "id": "clearance.rules",
      "title": "Clearance rules",
      "focused_cell": {
        "row_id": "row.netclass.default",
        "column_id": "clearance"
      },
      "columns": [
        { "id": "clearance", "label": "Clearance" }
      ],
      "rows": [
        {
          "id": "row.netclass.default",
          "label": "Default",
          "cells": {
            "clearance": "0.20 mm"
          }
        },
        {
          "id": "row.netclass.power",
          "label": "Power",
          "cells": {
            "clearance": ""
          }
        }
      ]
    }
  ]
}
```

6. A cell may be a string or an object with a string `value`.
7. The focused cell value must be non-empty.
8. At least two other rows in the same column must be empty before suggesting, to avoid noisy single-cell guesses.
9. The suggestion must include:
   - `m_ContextKind = "panel"`
   - `m_ContextDetailsJson` with reason `panel_table_fill`
   - title, body, fingerprint, and operation JSON
10. Operation JSON:

```json
{
  "operation": "panel_fill_column_preview",
  "panel_id": "board_setup.clearance",
  "table_id": "clearance.rules",
  "column_id": "clearance",
  "value": "0.20 mm",
  "target_row_ids": [
    "row.netclass.power",
    "row.netclass.signal"
  ]
}
```

11. The suggestion must not include edit objects yet.
12. Malformed, incomplete, non-panel, or low-confidence states must produce no suggestion.

## Design Choices

### Deterministic but non-applying

Panel edits need a UI-specific preview/apply adapter.  This slice creates the deterministic suggestion and operation contract only.  Accept/apply remains a later UI adapter task.

### Bounded schema

The provider accepts one simple table schema, making it easy for future panel capture code to emit a stable contract.

### Conservative threshold

Requiring at least two empty target rows prevents a single blank cell from triggering a broad column-fill suggestion.

## Non-goals

This slice does not add:

- UI control clicking
- panel edit application
- visual panel overlays
- a general table diff engine
- LLM-powered panel reasoning

## Self-review

- The provider consumes existing `panel_states` infrastructure.
- The operation is explicit and reviewable.
- The slice avoids unsafe UI mutations.
- The schema is small enough to test thoroughly.
