# AI PCB Semantic Anchor Generation Design

## Purpose

Make the PCB editor's context adapter populate the unified semantic-anchor channel from real board geometry. This turns the shared `AI_CONTEXT_SNAPSHOT` anchor fields into data the Agent can use for routing, placement, and later `go_to_anchor` tools without inventing a parallel PCB sensing API.

## Source Observations

- `pcbnew/kisurf_ai_pcb_context_adapter.cpp` already walks footprints, pads, routing tracks, drawings, tables, dimensions, and zones into `AI_OBJECT_REF` values.
- The adapter already emits structured JSON details for many geometry-bearing objects:
  - pads and vias expose `position`.
  - tracks expose `start` and `end`.
  - arcs expose `start`, `mid`, and `end`.
  - footprints, text, targets, barcodes, and zones expose `position`.
  - drawing shapes expose `start`, `end`, `center`, `radius_point`, or `position` depending on shape.
- `AI_CONTEXT_INDEX` now stores sorted `AI_CONTEXT_ANCHOR` values and carries them into `AI_CONTEXT_SNAPSHOT`.
- `AI_CONTEXT_SNAPSHOT` now serializes anchors into both prompt text and JSON.
- Existing PCB adapter tests live in `qa/tests/pcbnew/test_ai_pcb_context_adapter.cpp`; this is the right place to test editor-side anchor production.

## Goals

1. Populate semantic anchors from real PCB board objects during `KISURF_AI_PCB_CONTEXT_ADAPTER::BuildIndex()`.
2. Preserve one shared context path: PCB adapter -> `AI_CONTEXT_INDEX` -> `AI_CONTEXT_SNAPSHOT` -> model prompt/JSON.
3. Give each anchor a stable id, semantic kind, label, summary, board position, layer id when available, and JSON details.
4. Keep anchor generation deterministic and bounded enough for normal boards and later model context limits.
5. Add tests that prove pad/via/track/shape anchors are generated from board geometry and survive `BuildIndex()`.

## Non-Goals

- No `go_to_anchor` tool is added in this slice.
- No preview overlay rendering is added in this slice.
- No routing-candidate algorithm is added in this slice.
- No panel walker implementation is added in this slice.
- No schematic anchor generation is added in this slice.

## Anchor Model

PCB-generated anchors are facts about visible board geometry, not model-authored suggestions.

Each anchor must set:

- `m_Id`: stable, deterministic identifier based on object UUID and anchor role.
- `m_Kind`: semantic role from `AI_CONTEXT_ANCHOR_KIND`.
- `m_EditorKind`: `AI_EDITOR_KIND::Pcb`.
- `m_Label`: short human/model-readable label.
- `m_Summary`: one sentence describing what the anchor means.
- `m_Position`: board coordinates in internal units.
- `m_HasPosition`: true for every anchor in this slice.
- `m_Layer`: KiCad PCB layer id when the object has a single principal layer; `-1` when layer is ambiguous.
- `m_DetailsJson`: compact object metadata for grounding later tools.
- `m_Confidence`: `1.0` because these anchors are derived from concrete board geometry.

## Anchor Ids

Use lowercase dot-separated ids with the source object's UUID:

- Footprint position: `pcb.footprint.<uuid>.position`
- Pad center: `pcb.pad.<uuid>.center`
- Via center: `pcb.via.<uuid>.center`
- Track start: `pcb.track.<uuid>.start`
- Track end: `pcb.track.<uuid>.end`
- Arc start: `pcb.arc.<uuid>.start`
- Arc mid: `pcb.arc.<uuid>.mid`
- Arc end: `pcb.arc.<uuid>.end`
- Shape start: `pcb.shape.<uuid>.start`
- Shape end: `pcb.shape.<uuid>.end`
- Shape center: `pcb.shape.<uuid>.center`
- Shape position fallback: `pcb.shape.<uuid>.position`

These ids are intentionally independent of labels so display text changes do not reorder anchor identity.

## Anchor Kinds

- Footprint position uses `PlacementCandidate`.
- Pad center uses `RouteTarget`.
- Via center uses `RouteTarget`.
- Track start uses `RouteStart`.
- Track end uses `RouteTarget`.
- Arc start uses `RouteStart`.
- Arc mid uses `RouteCandidate`.
- Arc end uses `RouteTarget`.
- Shape start/end/center/position uses `ShapeCorner`.

The names are imperfect for all future uses, but they keep this slice inside the existing enum and give the Agent useful first-order semantics.

## Details JSON

Every generated anchor must include:

```json
{
  "source_object_uuid": "...",
  "source_label": "...",
  "source_type": 0,
  "role": "center",
  "position": { "x": 0, "y": 0 }
}
```

Routing anchors should also include:

```json
{
  "net_code": 1,
  "net_name": "/GND",
  "layer": "F.Cu"
}
```

Pad anchors should include:

```json
{
  "footprint_reference": "U1",
  "pad_number": "1",
  "net_code": 1,
  "net_name": "/GND"
}
```

Footprint anchors should include:

```json
{
  "reference": "U1",
  "value": "MCU",
  "footprint_id": "Package_QFP:TQFP-32"
}
```

## Generation Rules

The adapter must build an `anchors` vector alongside `visibleObjects` and `selectedObjects`.

Footprints:

- Add one footprint position anchor for every footprint.
- Use the footprint position and layer.
- The label should be `footprint:<reference>:position`, falling back to UUID if no reference exists.

Pads:

- Add one pad center anchor for every pad.
- Use `PAD::GetPosition()` and `PAD::GetPrincipalLayer()`.
- The label should be `pad:<footprint>.<pad>:center`, falling back to UUID when the pad has no composed label.

Routing objects:

- For vias, add one center anchor.
- For straight tracks, add start and end anchors.
- For arcs, add start, mid, and end anchors.
- Use the object layer for tracks and arcs; vias use `-1` because they span layers.

Drawing shapes:

- For segments and rectangles, add start and end anchors.
- For arcs, add start, mid, and end anchors.
- For circles, add center and radius-point anchors.
- For other shapes, add one position fallback anchor.
- Use the shape layer id.

Ordering and bounds:

- The adapter does not sort anchors directly; `AI_CONTEXT_INDEX::SetAnchors()` owns stable sorting.
- The adapter should add anchors only from objects it already exposes as visible context in this slice.
- No hard cap is required in the adapter because `AI_CONTEXT_SNAPSHOT::AsPromptText()` and `AsJsonText()` already apply model-facing bounds. A future board-scale optimization can cap producer-side anchors if profiling shows it is needed.

## Failure Behavior

- Invalid or deleted board items must not produce anchors.
- Objects with no meaningful position should produce no anchor in this slice.
- Empty labels are allowed only when the anchor id and details JSON still identify the source.
- Anchor generation must not mutate the board.

## Test Requirements

Add tests to `qa/tests/pcbnew/test_ai_pcb_context_adapter.cpp`:

1. `AdapterAddsPadAndViaSemanticAnchors`
   - Build a board with a net, a footprint pad, and a via.
   - Call `BuildIndex()`.
   - Verify `index.Anchors()` contains a pad center anchor and via center anchor with expected ids, kinds, positions, confidence, labels, and details JSON.
   - Verify `index.BuildSnapshot().m_Anchors` carries the same anchors.

2. `AdapterAddsRouteAndShapeSemanticAnchors`
   - Build a board with a track and a board-level shape.
   - Call `BuildIndex()`.
   - Verify track start/end anchors and shape start/end anchors exist with expected roles and positions.
   - Verify route anchors contain layer and net metadata.

## Verification Requirements

- Run red before production changes by building `qa_pcbnew`.
- Run green by building `qa_pcbnew` and running `AiPcbContextAdapter`.
- Run common AI regression suites because the anchor contract flows through common context types.
- Build `pcbnew`.
- Run whitespace and secret scans before commit.

## Self-Review

- Spec coverage: This slice turns the common anchor storage into real PCB editor context data without introducing tools or rendering.
- Scope boundary: It deliberately excludes prediction algorithms and anchor navigation tools so the editor-side sensing foundation remains reviewable.
- Source alignment: It reuses the existing PCB adapter walk and `AI_CONTEXT_INDEX::SetAnchors()` instead of creating a new board-scanning service.
- Risk check: Anchor ids are UUID-based and labels are display-only, so future label changes do not break model-facing identity.
