# AI PCB Zone And Keepout Context Design

Date: 2026-06-16

## Goal

Expose board-level `PCB_ZONE_T` objects to AI context and object resolution so the model can see copper pours, rule areas, and keepout restrictions that affect placement and routing decisions.

## Source Anchors

- `pcbnew/board.h` exposes board zones through `BOARD::Zones()`.
- `pcbnew/zone.h` defines `ZONE`, `GetIsRuleArea()`, `HasKeepoutParametersSet()`, keepout accessors, layer-set accessors, outline corners, priority, net code, and net name.
- Existing AI PCB context and resolver code lives in:
  - `pcbnew/kisurf_ai_pcb_context_adapter.cpp`
  - `pcbnew/kisurf_ai_pcb_object_resolver.cpp`

## Scope

This slice emits one `AI_OBJECT_REF` per board-level `ZONE` in `BOARD::Zones()`.

Each zone ref must include:

- Stable UUID and `PCB_ZONE_T` type.
- A readable label:
  - Copper zone with a zone name: `zone:<zone-name>`
  - Copper zone without a zone name but with a net: `zone:<net-name>`
  - Copper zone without either: `zone:<uuid>`
  - Rule area with keepout flags and name: `keepout:<zone-name>`
  - Rule area without keepout flags and with name: `rule-area:<zone-name>`
  - Rule or keepout area without a name: `rule-area:<uuid>` or `keepout:<uuid>`
- Details JSON describing the engineering semantics the model needs for placement and routing context.

## Details JSON Contract

Zone refs use `AI_OBJECT_REF.m_DetailsJson` with this shape:

```json
{
  "kind": "zone",
  "zone_kind": "copper",
  "name": "GND_POUR",
  "layers": ["F.Cu"],
  "first_layer": "F.Cu",
  "corner_count": 4,
  "position": { "x": 0, "y": 0 },
  "net_code": 1,
  "net_name": "/GND",
  "priority": 2,
  "is_rule_area": false,
  "has_keepout": false,
  "keepout": {
    "tracks": false,
    "vias": false,
    "pads": false,
    "footprints": false,
    "zone_fills": false
  }
}
```

`zone_kind` values are:

- `copper`: `GetIsRuleArea()` is false.
- `keepout`: `GetIsRuleArea()` is true and at least one keepout flag is set.
- `rule_area`: `GetIsRuleArea()` is true and no keepout flag is set.

Layer names must use the owning board's layer names when available.

## Resolver Contract

`KISURF_AI_PCB_OBJECT_RESOLVER::Resolve()` must support `PCB_ZONE_T` by scanning `BOARD::Zones()` and matching both UUID and type.

The preview and move edit adapters do not need new zone-specific code in this slice. They already operate on resolved `BOARD_ITEM` objects, and zones are `BOARD_ITEM`s.

## Non-Goals

- Do not expose filled polygon contours or hatch geometry.
- Do not expose zone holes, per-layer fill islands, triangulation caches, or zone filler internals.
- Do not refill zones or change copper connectivity.
- Do not add zone vertex editing, creation, deletion, or property mutation.
- Do not expose footprint-local zones in this slice.

## Acceptance Criteria

- A named selected copper zone appears in visible and selected AI context.
- Copper zone details include name, layer names, corner count, net code, net name, priority, rule-area state, keepout state, and position.
- A named selected rule-area keepout appears with a `keepout:<name>` label and keepout flags in details JSON.
- `PCB_ZONE_T` refs resolve back to the original `ZONE*`.
- Existing PCB AI context, resolver, preview, and move edit tests remain green.
