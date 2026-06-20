# AI PCB Arc Context Coverage Design

Date: 2026-06-16

## Problem

PCB context now exposes footprints, pads, straight tracks, and vias, but it still omits routed arc
segments. KiCad treats `PCB_ARC_T` as a copper routing object alongside tracks and vias, so the
model cannot yet see or resolve curved route segments that an engineer can select and edit.

## Goals

- Expose PCB arc object references in model-visible context.
- Include selected arcs in selected object context.
- Resolve arc references through the PCB object resolver.

## Non-Goals

- No net, width, layer, radius, angle, or clearance metadata expansion in this slice.
- No automatic route editing or arc creation.
- No change to preview or edit permission policy.

## Design

`KISURF_AI_PCB_CONTEXT_ADAPTER` will treat `PCB_ARC_T` entries from `BOARD::Tracks()` as routing
objects. Arc labels use `arc:<start-x>,<start-y>-><mid-x>,<mid-y>-><end-x>,<end-y>` so prompt text
is deterministic and distinguishes curved segments from straight track labels. UUID and object type
remain the authoritative identity.

`KISURF_AI_PCB_OBJECT_RESOLVER` will include `PCB_ARC_T` in the routing-ref type set and resolve
arcs by UUID and type from `BOARD::Tracks()`.

## Verification

- Add context adapter coverage for visible and selected arcs.
- Add object resolver coverage for arc refs.
- Re-run PCB AI context, resolver, preview, and move edit tests.

## Self Review

- Scope is limited to model-visible sensing and object resolution.
- Arc labels are deterministic and do not require geometric reconstruction.
- Rich routing metadata remains a later schema slice instead of being hidden in labels.
