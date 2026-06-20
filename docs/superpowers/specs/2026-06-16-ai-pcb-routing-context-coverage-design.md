# AI PCB Routing Context Coverage Design

Date: 2026-06-16

## Problem

PCB context now exposes footprints and pads, but it still omits board-level tracks and vias. That
means the model cannot directly see or resolve the routing objects needed for single-trace review,
routing previews, or via-aware suggestions.

## Goals

- Expose PCB track and via object references in model-visible context.
- Include selected tracks and vias in selected object context.
- Resolve track and via references through the PCB object resolver.

## Non-Goals

- No arc-track coverage in this slice.
- No net, width, layer, or clearance metadata expansion yet.
- No routing algorithm or automatic router integration.

## Design

`KISURF_AI_PCB_CONTEXT_ADAPTER` will add refs for `PCB_TRACE_T` and `PCB_VIA_T` entries from
`BOARD::Tracks()`. Track labels use `track:<start-x>,<start-y>-><end-x>,<end-y>`. Via labels use
`via:<x>,<y>`. UUID and object type remain the authoritative identity.

`KISURF_AI_PCB_OBJECT_RESOLVER` will resolve `PCB_TRACE_T` and `PCB_VIA_T` refs by UUID from
`BOARD::Tracks()`.

## Verification

- Add context adapter coverage for visible and selected tracks/vias.
- Add object resolver coverage for track and via refs.
- Re-run PCB AI context, resolver, preview, and move edit tests.

## Self Review

- This improves routing awareness without changing mutation policy.
- Labels are deterministic and human-readable enough for prompt context.
- Arc and richer route metadata remain explicitly scoped follow-ups.
