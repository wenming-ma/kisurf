# AI PCB Component And Pad Details Design

Date: 2026-06-16

## Problem

PCB context refs now cover footprints, pads, tracks, arcs, and vias. Routing refs include
structured geometry and net details, but footprint and pad refs still expose mostly labels. The
model can identify `U1` and `U1.1`, but it cannot inspect component placement, footprint identity,
pad geometry, pad layer, or pad net from the context payload.

## Goals

- Populate `AI_OBJECT_REF.m_DetailsJson` for PCB footprints and pads.
- Preserve existing footprint and pad labels.
- Keep object resolution based on UUID plus `KICAD_T`; details remain observational.
- Include compact fields useful for component placement and routing decisions.

## Non-Goals

- Do not expose full courtyard, 3D model, DRC, ratsnest, or connectivity graph data.
- Do not add preview/edit behavior or change accepted edit materialization.
- Do not change route details added by the previous structured details slice.

## Design

`KISURF_AI_PCB_CONTEXT_ADAPTER` will continue to emit footprint and pad refs while adding optional
details.

Footprint refs include:

- `kind`: `"footprint"`.
- `reference`, `value`, and `footprint_id`.
- `position` as `{ "x": int, "y": int }`.
- `orientation_degrees`.
- `layer`.
- `pad_count`.

Pad refs include:

- `kind`: `"pad"`.
- `footprint_reference` and `number`.
- `position` as `{ "x": int, "y": int }`.
- `size` as `{ "x": int, "y": int }`.
- `drill` as `{ "x": int, "y": int }`.
- `shape`, `layer`, `orientation_degrees`.
- `net_code` and `net_name`.

Pad shape strings are stable KiCad-style tokens: `circle`, `rect`, `oval`, `trapezoid`,
`roundrect`, `chamfered_rect`, `custom`, or `unknown`.

## Verification

- Add RED PCB context adapter coverage that requires footprint details and pad details.
- Verify the test fails before implementation because the details string is missing.
- Implement the smallest adapter changes needed to populate details.
- Re-run targeted PCB AI context, resolver, preview, and move-edit tests.

## Self Review

- This slice extends the existing structured details channel without expanding identity or edit
  semantics.
- The field set is intentionally compact and avoids expensive geometry or connectivity traversal.
- Richer ratsnest, nearest-neighbor, and component-placement intelligence remain follow-up slices.
