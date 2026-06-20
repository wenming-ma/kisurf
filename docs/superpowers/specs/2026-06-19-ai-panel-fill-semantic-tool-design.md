# AI Panel Fill Semantic Tool Design

Date: 2026-06-19

## Goal

Add a chat-accessible semantic tool that creates a reviewable panel column-fill preview suggestion.  This complements the background Agent's deterministic panel table suggestion path by letting the model explicitly request the same class of panel preview from the chat flow.

## Problem

The next-action pipeline can now emit `panel_fill_column_preview` suggestions from focused panel table state.  However, the semantic tool surface exposed to the model does not include a panel-fill preview tool, and `ParseAiSuggestionOperation()` does not recognize the panel-fill operation.  That creates a split: background prediction understands the operation contract, while chat/tool-call workflows cannot create or validate it through the common tool pipeline.

## Requirements

1. Add a new operation kind named `PanelFillColumnPreview`.
2. Parse `panel_fill_column_preview` operation JSON with these fields:
   - `panel_id`: non-empty string
   - `table_id`: non-empty string
   - `column_id`: non-empty string
   - `value`: non-empty string
   - `target_row_ids`: non-empty array of non-empty strings
3. Expose parsed fields on `AI_SUGGESTION_OPERATION`.
4. Add a semantic tool named `kisurf_preview_panel_fill_column`.
5. The tool arguments must mirror the operation JSON except the handler adds `operation`.
6. The handler must validate that:
   - the current context contains `panel_id`
   - the panel state JSON contains `table_id`
   - the table schema contains `column_id` when columns are present
   - every `target_row_ids` entry is present in the table rows
7. The handler must create a preview suggestion with:
   - `m_ContextKind = "panel"` when the current dynamic context is panel
   - `m_ContextDetailsJson` reason `panel_fill_column`
   - explicit operation JSON in `m_ArgumentsJson`
   - no preview or edit objects yet
8. The OpenAI-compatible tool catalog must declare the new function schema.
9. Malformed fields, unknown panels, unknown tables, unknown columns, and unknown rows must be denied without storing a suggestion.

## Design Choices

### Tool-call path, not UI mutation

This slice creates a preview suggestion only.  It does not click controls, edit a grid, or apply panel changes.  The result remains reviewable and requires a future panel adapter for apply/accept behavior.

### Grounded IDs

The model must provide explicit panel, table, column, and row IDs.  The semantic handler checks these IDs against the current panel state to reduce hallucinated UI operations.

### Parser stays syntactic

`ParseAiSuggestionOperation()` validates the operation shape and stores parsed fields.  Context-specific validation stays in the semantic tool handler because only the handler has the current panel state.

## Non-goals

This slice does not add:

- panel edit application
- live panel grid mutation
- visual overlays for panel cells
- automatic row discovery by the model
- a generic panel table edit engine

## Self-review

- The design keeps chat and background Agent suggestion contracts aligned.
- The tool is preview-only, so it does not bypass user approval.
- Context validation is strong enough to catch hallucinated panel/table/row IDs.
- The implementation is small enough to TDD through parser, handler, and provider-schema tests.
