# AI PCB Drawing Shape Context Design

Date: 2026-06-16

## Problem

The PCB context adapter now exposes components, pads, and routing objects, but it still omits
board-level drawing shapes. This means the model cannot inspect board-edge segments on `Edge.Cuts`,
mechanical graphics, copper graphic shapes, or selected drawing geometry even though engineers see
and edit these objects directly in the PCB editor.

## Goals

- Expose board-level `PCB_SHAPE_T` items from `BOARD::Drawings()` as AI object refs.
- Include compact structured details for common shape geometry.
- Resolve shape refs back to native `PCB_SHAPE` items so preview/edit adapters can reuse existing
  clone and move behavior.
- Preserve existing footprint, pad, and routing context behavior.

## Non-Goals

- Do not expose zones, keepouts, filled polygons, or rule-area semantics in this slice.
- Do not compute board outline chains or closed contours from multiple edge segments.
- Do not add special edit operations for shape vertices.

## Design

`KISURF_AI_PCB_CONTEXT_ADAPTER` will iterate `BOARD::Drawings()` and emit refs for items whose
native type is `PCB_SHAPE_T`.

Labels should be readable:

- Edge-cut segments use `edge:x1,y1->x2,y2`.
- Non-edge segments use `shape:segment:x1,y1->x2,y2`.
- Arcs use `shape:arc:x1,y1->xm,ym->x2,y2`.
- Rectangles use `shape:rect:x1,y1->x2,y2`.
- Circles use `shape:circle:xc,yc->xr,yr`.
- Unknown shapes fall back to `shape:<uuid>`.

Details include:

- `kind`: `"shape"`.
- `shape`: `segment`, `arc`, `rect`, `circle`, `poly`, `bezier`, `ellipse`, `ellipse_arc`, or
  `unknown`.
- `layer`, `width`, `net_code`, and `net_name`.
- Geometry fields appropriate to the shape: `start`, `end`, `mid`, `center`, `radius_point`,
  and `radius`.

`KISURF_AI_PCB_OBJECT_RESOLVER` will support `PCB_SHAPE_T` by scanning `BOARD::Drawings()` and
matching UUID plus native type.

## Verification

- Add RED PCB context adapter coverage for a selected Edge.Cuts segment and a structured details
  check.
- Add RED PCB object resolver coverage for resolving a shape ref from the context adapter.
- Implement context and resolver support.
- Re-run targeted PCB AI context, resolver, preview, and move-edit tests.

## Self Review

- This closes a visible-board-context gap without pulling in zone fill complexity.
- Shape details are observational and do not become resolver identity.
- Zone, keepout, board-outline chaining, and shape vertex editing remain follow-up slices.
