# AI PCB Fabrication Annotation Context Design

Date: 2026-06-16

## Goal

Expose board-level fabrication and mechanical annotation objects to AI context and object resolution so the model can perceive targets, barcodes, tables, table cells, and dimensions that communicate manufacturing, assembly, inspection, and mechanical constraints.

## Source Anchors

- `pcbnew/board.h` exposes board drawings through `BOARD::Drawings()`.
- `pcbnew/pcb_target.h` defines `PCB_TARGET`.
- `pcbnew/pcb_barcode.h` defines `PCB_BARCODE`.
- `pcbnew/pcb_table.h` defines `PCB_TABLE`.
- `pcbnew/pcb_tablecell.h` defines `PCB_TABLECELL`.
- `pcbnew/pcb_dimension.h` defines `PCB_DIMENSION_BASE` and concrete dimension types such as `PCB_DIM_ALIGNED`.
- `include/core/typeinfo.h` exposes `BaseType()` for dimension meta-type checks.

## Scope

This slice emits AI object refs for board-owned annotation objects:

- `PCB_TARGET_T`
- `PCB_BARCODE_T`
- `PCB_TABLE_T`
- `PCB_TABLECELL_T` children owned by board-level tables
- Any concrete dimension type whose `BaseType( Type() ) == PCB_DIMENSION_T`

Footprint-local barcode/table/dimension objects remain out of scope for this slice.

## Object Labels

Labels must be readable and stable enough for model references:

- `target:<x>,<y>`
- `barcode:<preview>`
- `table:<x>,<y>`
- `table-cell:<addr>`
- `dimension:<start-x>,<start-y>-><end-x>,<end-y>`

Barcode previews come from `PCB_BARCODE::GetText()`, normalized like text previews. If an annotation label cannot derive a readable subject, use the item UUID.

## Details JSON Contract

Target refs use:

```json
{
  "kind": "target",
  "position": { "x": 100, "y": 200 },
  "layer": "User.Drawings",
  "shape": 0,
  "size": 1000,
  "width": 100
}
```

Barcode refs use:

```json
{
  "kind": "barcode",
  "text": "SN-001",
  "shown_text": "SN-001",
  "barcode_kind": "qr_code",
  "position": { "x": 100, "y": 200 },
  "layer": "F.Silkscreen",
  "width": 2000,
  "height": 2000,
  "show_text": true,
  "angle_degrees": 0
}
```

Table refs use:

```json
{
  "kind": "table",
  "position": { "x": 0, "y": 0 },
  "end": { "x": 2000, "y": 1000 },
  "layer": "User.Comments",
  "columns": 2,
  "rows": 1,
  "cell_count": 2,
  "border_width": 100,
  "separators_width": 100
}
```

Table cell refs reuse the textbox-style text details and add table metadata:

```json
{
  "kind": "table_cell",
  "text": "Part",
  "shown_text": "Part",
  "address": "A1",
  "row": 0,
  "column": 0,
  "parent_table_uuid": "...",
  "row_span": 1,
  "col_span": 1
}
```

Dimension refs use:

```json
{
  "kind": "dimension",
  "dimension_type": "aligned",
  "text": "10.00 mm",
  "shown_text": "10.00 mm",
  "start": { "x": 0, "y": 0 },
  "end": { "x": 1000, "y": 0 },
  "layer": "User.Drawings",
  "measured_value": 1000,
  "line_thickness": 100
}
```

## Resolver Contract

`KISURF_AI_PCB_OBJECT_RESOLVER::Resolve()` must support:

- `PCB_TARGET_T`, `PCB_BARCODE_T`, `PCB_TABLE_T`, and all concrete dimension types by scanning `BOARD::Drawings()` and matching UUID plus exact type.
- `PCB_TABLECELL_T` by scanning cells of every board-level `PCB_TABLE_T` and matching UUID plus exact type.

The resolver must not resolve a base `PCB_DIMENSION_T` ref to a concrete dimension unless the ref's `m_Type` matches the concrete item type. This keeps model refs exact.

## Non-Goals

- Do not expose footprint-local annotation objects in this slice.
- Do not expose barcode rendered polygons.
- Do not expose full dimension generated geometry, arrow segments, or glyph polygons.
- Do not edit annotation content, table cells, or dimension geometry.
- Do not change existing board-level shape, text, textbox, zone, footprint, or routing contracts.

## Acceptance Criteria

- Selected board-level targets, barcodes, tables, table cells, and dimensions appear in visible and selected AI context.
- Details JSON includes enough text, layer, geometry, and parent metadata for model grounding.
- `PCB_TARGET_T`, `PCB_BARCODE_T`, `PCB_TABLE_T`, `PCB_TABLECELL_T`, and concrete dimension refs resolve back to original native objects.
- Existing PCB AI context, resolver, preview, and move edit tests remain green.
