# AI Operation-Only Preview Controls Design

Date: 2026-06-19

## Goal

Make the Agent panel's Preview and Accept controls reflect the active suggestion's actual capabilities.  Operation-only preview suggestions, such as panel table fill previews, should be previewable even when they do not contain canvas preview objects, while Accept should remain disabled until an edit adapter exists.

## Problem

`AI_SUGGESTION_ORCHESTRATOR::BeginPreview()` currently rejects suggestions with empty `m_PreviewObjects`.  This works for PCB/schematic object previews, but panel suggestions intentionally carry a validated operation JSON without board objects.  The Agent panel also enables Preview and Accept based only on whether handlers are installed, so operation-only suggestions can make controls appear available even when Accept cannot do anything.

## Requirements

1. Add capability checks for an individual suggestion:
   - preview is available when the suggestion is active and has preview objects or a recognized preview operation JSON
   - accept is available only when the suggestion is active and has edit objects
2. Add model-level wrappers so UI code can ask whether a specific suggestion can preview or accept.
3. Update `AI_SUGGESTION_ORCHESTRATOR::BeginPreview()` to allow operation-only preview suggestions.  It should start the preview session and mark the suggestion `Previewing`, but it should not show any object when `m_PreviewObjects` is empty.
4. Keep `Accept()` conservative: no edit objects means no accept.
5. Update Agent panel semantic state and buttons to use per-suggestion capability checks.
6. Cover object previews, operation-only previews, and accept rejection with tests.

## Design Choices

### Preview is review state

For operation-only suggestions, preview means "enter the explicit review state" rather than draw a canvas object.  This matches panel table suggestions, where the operation is textual/semantic until a future panel adapter applies it.

### Accept remains adapter-backed

Accept should not become a no-op success.  It remains tied to edit objects and an edit adapter so the UI cannot claim a panel change was applied before a real panel adapter exists.

### Shared capability source

The orchestrator owns the canonical capability decision.  The panel model and panel UI read that decision instead of duplicating object-count checks.

## Non-goals

This slice does not add:

- panel cell mutation
- panel adapter implementation
- visual overlays for panel operations
- model-generated auto-accept

## Self-review

- The design fixes button availability without weakening edit safety.
- Operation-only panel suggestions become reviewable immediately.
- Accept remains unavailable for suggestions with no edit objects.
- The change is small and testable at orchestrator and model levels.
