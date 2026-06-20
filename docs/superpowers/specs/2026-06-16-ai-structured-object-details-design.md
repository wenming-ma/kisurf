# AI Structured Object Details Design

Date: 2026-06-16

## Problem

Model-visible object refs now cover the first PCB object families, but each ref only carries
`label`, `type`, and `uuid`. That is enough to identify and resolve an object, but not enough for
the model to reason about route geometry, layer, width, or net without guessing from labels.

## Goals

- Add an optional structured details channel to `AI_OBJECT_REF`.
- Include details in prompt text and structured context JSON.
- Populate PCB routing refs with compact details for tracks, arcs, and vias.

## Non-Goals

- Do not make details part of resolver identity.
- Do not define the full project ontology in this slice.
- Do not expose visual pixels, DRC, ratsnest, or connectivity graph details here.

## Design

`AI_OBJECT_REF` gains an optional `m_DetailsJson` string. It stores a compact JSON object owned by
the context producer. Existing refs remain valid without details.

Prompt context appends `details=<json>` when details are present. Structured context JSON parses
valid detail strings into a nested `details` object on each ref. Empty details are omitted.

`KISURF_AI_PCB_CONTEXT_ADAPTER` will populate route details for:

- Tracks: `kind`, `start`, `end`, `layer`, `width`, `net_code`, `net_name`.
- Arcs: `kind`, `start`, `mid`, `end`, `layer`, `width`, `net_code`, `net_name`.
- Vias: `kind`, `position`, `diameter`, `net_code`, `net_name`.

Details are observational metadata only. UUID and KICAD_T remain the authoritative identity for
preview, edit, and resolver paths.

## Verification

- Add common AI type coverage for details in prompt and structured JSON context.
- Add PCB context adapter coverage for route details on track, arc, and via refs.
- Re-run targeted common and PCB AI context tests.

## Self Review

- The slice adds a general channel but only populates a narrow route-focused field set.
- Details are explicitly non-authoritative for identity, avoiding geometry-based resolver behavior.
- Richer schematic, pad, footprint, DRC, and graph metadata remain follow-up slices.
