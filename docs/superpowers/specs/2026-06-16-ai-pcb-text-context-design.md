# AI PCB Text Context Design

Date: 2026-06-16

## Goal

Expose board-level `PCB_TEXT_T` and `PCB_TEXTBOX_T` drawing objects to AI context and object resolution so the model can read user-visible PCB annotations, silkscreen labels, fabrication notes, and boxed text notes.

## Source Anchors

- `pcbnew/board.h` exposes board drawings through `BOARD::Drawings()`.
- `pcbnew/pcb_text.h` defines `PCB_TEXT`.
- `pcbnew/pcb_textbox.h` defines `PCB_TEXTBOX`.
- `include/eda_text.h` defines common text accessors such as `GetText()`, `GetShownText()`, `GetTextSize()`, `GetTextAngle()`, `IsVisible()`, `IsMirrored()`, `IsBold()`, `IsItalic()`, `GetHorizJustify()`, and `GetVertJustify()`.

## Scope

This slice emits AI object refs for board-level text drawings:

- `PCB_TEXT_T`
- `PCB_TEXTBOX_T`

Footprint reference/value fields and footprint-local text remain represented through footprint details or future footprint-child context slices.

## Object Labels

Labels must be readable but bounded:

- `text:<preview>`
- `textbox:<preview>`

`<preview>` is generated from `GetText()`, with newlines and tabs replaced by spaces. If the preview is empty, use the item UUID. If the preview is longer than 48 characters, truncate it to 48 characters and append `...`.

## Details JSON Contract

Text refs use `AI_OBJECT_REF.m_DetailsJson` with this common shape:

```json
{
  "kind": "text",
  "text": "JTAG HEADER",
  "shown_text": "JTAG HEADER",
  "position": { "x": 100, "y": 200 },
  "size": { "x": 1200, "y": 1200 },
  "layer": "F.SilkS",
  "angle_degrees": 90,
  "visible": true,
  "mirrored": false,
  "bold": true,
  "italic": false,
  "h_justify": "center",
  "v_justify": "center"
}
```

Textbox refs use `kind: "textbox"` and add box geometry:

```json
{
  "kind": "textbox",
  "text": "Assembly note",
  "shown_text": "Assembly note",
  "position": { "x": 0, "y": 0 },
  "size": { "x": 1000, "y": 500 },
  "layer": "Cmts.User",
  "angle_degrees": 0,
  "visible": true,
  "mirrored": false,
  "bold": false,
  "italic": false,
  "h_justify": "left",
  "v_justify": "top",
  "start": { "x": 0, "y": 0 },
  "end": { "x": 2000, "y": 1000 },
  "border_enabled": true,
  "border_width": 100
}
```

Justification values are `left`, `center`, `right`, `top`, and `bottom`.

## Resolver Contract

`KISURF_AI_PCB_OBJECT_RESOLVER::Resolve()` must support `PCB_TEXT_T` and `PCB_TEXTBOX_T` by scanning `BOARD::Drawings()` and matching both UUID and exact type.

## Non-Goals

- Do not expose `PCB_TABLE_T`, `PCB_TABLECELL_T`, `PCB_BARCODE_T`, or `PCB_DIMENSION_T` in this slice.
- Do not expand text variables beyond `GetShownText()`.
- Do not expose rendered glyph polygons.
- Do not edit text content or text style.
- Do not expose footprint-local text in this slice.

## Acceptance Criteria

- A selected board-level `PCB_TEXT_T` appears in visible and selected AI context.
- Text details include raw text, shown text, position, size, layer, rotation, visibility, mirroring, style, and justification.
- A selected board-level `PCB_TEXTBOX_T` appears in visible and selected AI context.
- Textbox details include the common text fields plus start/end and border metadata.
- `PCB_TEXT_T` and `PCB_TEXTBOX_T` refs resolve back to the original drawing objects.
- Existing PCB AI context, resolver, preview, and move edit tests remain green.
