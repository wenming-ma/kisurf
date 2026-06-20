# AI Context Object Stable Order Design

Date: 2026-06-16

## Problem

`AI_CONTEXT_INDEX` currently preserves whatever object order each editor adapter provides. Native
containers may be stable in many cases, but model-facing context should not depend on incidental
iteration order, especially when context is bounded before being sent to a provider.

## Goals

- Make visible and selected object lists deterministic.
- Apply the same ordering rule to PCB, schematic, and future context producers.
- Preserve object identity, labels, and version bump behavior.

## Non-Goals

- No change to which objects are indexed.
- No viewport geometry sorting in this slice.
- No deduplication or object ranking beyond stable ordering.

## Design

`AI_CONTEXT_INDEX::SetVisibleObjects` and `SetSelectedObjects` will sort their input before storing
it. The ordering key is:

1. label, case-insensitive with case-sensitive tie break
2. object type
3. UUID string

This keeps the context deterministic without requiring editor-specific adapters to share sorting
code.

## Verification

- Add unit coverage proving visible and selected objects are sorted before snapshot creation.
- Re-run context index and nearby AI serialization/provider tests.

## Self Review

- Sorting is read-only and does not mutate editor objects.
- Version revisions still bump exactly once per setter call.
- Future semantic ranking can layer on top of this stable baseline.
