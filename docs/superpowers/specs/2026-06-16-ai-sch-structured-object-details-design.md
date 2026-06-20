# AI Schematic Structured Object Details Design

Date: 2026-06-16

## Problem

The schematic context adapter enumerates `SCH_ITEM` objects, but most refs only expose a
human-oriented label and native type. The model cannot reliably inspect symbol fields, wire or bus
endpoints, or label text without guessing from `GetFriendlyName()`.

## Goals

- Populate `AI_OBJECT_REF.m_DetailsJson` for common schematic objects.
- Keep schematic resolver identity as UUID plus KICAD_T.
- Improve model-readable labels for wires, buses, and schematic labels.
- Cover symbols, wires, buses, local/global/hierarchical labels, junctions, and no-connect markers.

## Non-Goals

- Do not compute full schematic connectivity or resolved net names in this slice.
- Do not expose symbol pin lists or library graphics yet.
- Do not change preview, edit, or resolver behavior.

## Design

`KISURF_AI_SCH_CONTEXT_ADAPTER` will keep collecting every `SCH_ITEM` from the screen, but each
ref may include compact details:

- Symbols: `kind`, `reference`, `value`, `footprint`, `position`.
- Wires and buses: `kind`, `start`, `end`, `layer`.
- Local/global/hierarchical labels: `kind`, `text`, `position`.
- Junctions: `kind`, `position`.
- No-connect markers: `kind`, `position`.

Labels should be stable and readable:

- Symbols use their reference field when present.
- Lines use `wire:x1,y1->x2,y2`, `bus:x1,y1->x2,y2`, or `line:x1,y1->x2,y2`.
- Labels use `<kind>:<text>` for local, global, and hierarchical labels.
- Junctions and no-connect markers use `<kind>:x,y`.
- Other items continue to fall back to friendly name and UUID.

Details are observational metadata only. They do not participate in object resolution,
preview, or edit materialization.

## Verification

- Add schematic context adapter coverage that checks details for a symbol, wire, bus, labels,
  junction, and no-connect marker.
- Re-run targeted eeschema AI context, resolver, preview, and move-edit tests.

## Self Review

- This improves model-visible schematic sensing without taking a dependency on the connectivity
  graph or hierarchy expansion.
- Full resolved net, pin, sheet, and ERC metadata remain follow-up slices.
