# AI PCB Footprint Context Coverage Design

Date: 2026-06-16

## Problem

The PCB context adapter currently exposes pads from footprints but not the footprint objects
themselves. A user can place, select, or move a footprint while the model-visible context only shows
its pads, which weakens component-level sensing and preview flows.

## Goals

- Expose each PCB footprint as a model-visible object reference.
- Include selected footprints in selected object context.
- Resolve footprint object references through the PCB object resolver so preview and edit adapters
  can work at component level.

## Non-Goals

- No track, via, zone, or graphic expansion in this slice.
- No footprint field/property JSON expansion.
- No ranking beyond the existing stable object ordering.

## Design

`KISURF_AI_PCB_CONTEXT_ADAPTER` will add one footprint-level `AI_OBJECT_REF` per footprint before
adding its pad references. The footprint label uses the footprint reference when present and falls
back to `footprint:<uuid>`.

`KISURF_AI_PCB_OBJECT_RESOLVER` will resolve `PCB_FOOTPRINT_T` references by UUID in addition to
existing pad resolution.

## Verification

- Add context adapter coverage for visible and selected footprints.
- Add object resolver coverage for footprint references.
- Re-run PCB AI context, resolver, preview, and move edit tests.

## Self Review

- This expands model context without changing editor mutation behavior.
- Existing pad references remain available for pin-level work.
- Preview and edit adapters keep their existing generic `BOARD_ITEM` path.
