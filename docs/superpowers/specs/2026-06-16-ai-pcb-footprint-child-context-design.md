# AI PCB Footprint Child Context Design

Date: 2026-06-16

## Goal

Expose footprint-local fields, text, text boxes, and graphic shapes to AI context and object resolution so the model can perceive the same reference/value labels, assembly text, pin-1 marks, courtyard/silkscreen graphics, and other footprint annotations that a PCB engineer sees around a placed component.

## Source Anchors

- `pcbnew/footprint.h` exposes placed footprints, pads, fields, and footprint-local graphic items.
- `FOOTPRINT::GetFields()` returns `PCB_FIELD_T` text fields such as reference, value, datasheet, and user fields.
- `FOOTPRINT::GraphicalItems()` returns footprint-local board items, including `PCB_SHAPE_T`, `PCB_TEXT_T`, and `PCB_TEXTBOX_T`.
- `pcbnew/pcb_field.h` defines `PCB_FIELD`.
- `pcbnew/pcb_shape.h`, `pcbnew/pcb_text.h`, and `pcbnew/pcb_textbox.h` define the already-supported board-level drawing primitives.

## Scope

This slice emits AI object refs for footprint-owned visible primitives:

- `PCB_FIELD_T` from `FOOTPRINT::GetFields()`.
- Footprint-local `PCB_SHAPE_T`, `PCB_TEXT_T`, and `PCB_TEXTBOX_T` from `FOOTPRINT::GraphicalItems()`.

Board-level shapes, text, and text boxes remain covered by the board drawing context slices. Pads and footprint refs remain covered by the component/pad context slices.

## Object Labels

Footprint child labels must be readable, bounded, and parent-qualified:

- `fp:<footprint-reference>/field:<field-name>`
- `fp:<footprint-reference>/text:<preview>`
- `fp:<footprint-reference>/textbox:<preview>`
- `fp:<footprint-reference>/<shape-label>`

If the footprint reference is empty, use the footprint UUID. Shape labels reuse the existing board-level shape label format after the footprint prefix. Text previews use the same normalization as board-level text: replace newlines and tabs with spaces, trim, fall back to UUID when empty, and truncate previews longer than 48 characters with `...`.

## Details JSON Contract

Every footprint child ref must include parent metadata:

```json
{
  "parent_footprint_reference": "U1",
  "parent_footprint_uuid": "..."
}
```

Field refs use the common text details plus field metadata:

```json
{
  "kind": "field",
  "text": "U1",
  "shown_text": "U1",
  "position": { "x": 100, "y": 200 },
  "size": { "x": 1200, "y": 900 },
  "layer": "F.Silkscreen",
  "angle_degrees": 0,
  "visible": true,
  "mirrored": false,
  "bold": false,
  "italic": false,
  "h_justify": "center",
  "v_justify": "center",
  "parent_footprint_reference": "U1",
  "parent_footprint_uuid": "...",
  "field_name": "Reference",
  "field_canonical_name": "Reference",
  "field_id": 0,
  "field_ordinal": 0,
  "is_reference": true,
  "is_value": false
}
```

Footprint-local `PCB_TEXT_T` and `PCB_TEXTBOX_T` reuse the board-level text details and add the parent fields. Footprint-local `PCB_SHAPE_T` reuses the board-level shape details and adds the parent fields.

## Resolver Contract

`KISURF_AI_PCB_OBJECT_RESOLVER::Resolve()` must support:

- `PCB_FIELD_T` by scanning each footprint's `GetFields()` list and matching UUID plus exact type.
- Footprint-local `PCB_SHAPE_T`, `PCB_TEXT_T`, and `PCB_TEXTBOX_T` by scanning each footprint's `GraphicalItems()` after board-level drawing lookup.

The resolver must continue to resolve board-level drawing refs from `BOARD::Drawings()` and must not confuse two objects with the same type but different UUIDs.

## Non-Goals

- Do not expose footprint-local `PCB_BARCODE_T`, `PCB_TABLE_T`, `PCB_TABLECELL_T`, `PCB_DIMENSION_T`, or image refs in this slice.
- Do not mutate footprint fields or graphics.
- Do not add model-visible edit tools for field/text content.
- Do not infer rendered glyph polygons or shape boolean geometry.
- Do not change footprint or pad labels already exposed by previous slices.

## Acceptance Criteria

- A selected footprint reference/value/user field appears in visible and selected AI context as `PCB_FIELD_T`.
- Field details include common text metadata, parent footprint metadata, field name/canonical name, id, ordinal, and reference/value booleans.
- A selected footprint-local `PCB_SHAPE_T` appears in visible and selected AI context with parent metadata.
- A selected footprint-local `PCB_TEXT_T` appears in visible and selected AI context with parent metadata.
- `PCB_FIELD_T`, footprint-local `PCB_SHAPE_T`, and footprint-local `PCB_TEXT_T` refs resolve back to the original native objects.
- Existing PCB AI context, resolver, preview, and move edit tests remain green.
