# AI Native Foundation Design

Date: 2026-06-16

## Purpose

This spec defines the shared native AI foundation for KiSurf. It creates the internal runtime and data contracts that later Agent UI, context observation, preview, materialization, validation, and external interfaces will use.

The foundation must make AI a first-class native collaborator while keeping KiCad's project model deterministic, auditable, and undoable.

## Goals

- Provide a native module namespace for KiSurf AI code.
- Define stable internal data types for context versions, object references, suggestions, preview plans, edit plans, validation summaries, and trace events.
- Provide a deterministic stub AI provider so the UI and edit loop can be tested without cloud credentials.
- Keep provider integration replaceable so OpenAI or other multimodal model providers can be added after the native surface is stable.
- Ensure no model output can directly mutate KiCad documents without passing through a reviewed edit session.

## Non-Goals

- No production OpenAI API integration in the first implementation task.
- No autonomous PCB placement or routing algorithm in this foundation spec.
- No external IPC/MCP protocol changes in the first implementation task.
- No UI automation semantic-tree implementation in this foundation spec.

## Proposed Files

The implementation plan should use exact paths after checking the current build layout, but the intended module shape is:

- Create `include/kisurf/ai/ai_types.h`
  - Value types and enums shared by AI modules.
- Create `common/kisurf/ai/ai_types.cpp`
  - Formatting, comparison, and validation helpers for AI value types.
- Create `include/kisurf/ai/ai_provider.h`
  - Provider interface for deterministic stub and future model-backed providers.
- Create `common/kisurf/ai/ai_provider.cpp`
  - Stub provider implementation.
- Create `include/kisurf/ai/ai_runtime.h`
  - Runtime interface used by Agent UI and editor modules.
- Create `common/kisurf/ai/ai_runtime.cpp`
  - Runtime orchestration, request IDs, cancellation, and trace recording hooks.
- Create tests under an existing unit-test area selected by the implementation plan.

If KiCad's CMake layout requires a different location, the implementation plan must name the exact library target and update only the smallest necessary CMake files.

## Core Types

### `AI_CONTEXT_VERSION`

Represents the state a suggestion was built against.

Fields:

- `uint64_t boardRevision`
- `uint64_t schematicRevision`
- `uint64_t connectivityRevision`
- `uint64_t viewRevision`
- `wxString documentKey`
- `wxString sheetPath`
- `uint32_t schemaVersion`

Behavior:

- Default construction creates an invalid version.
- `IsValid()` returns true only when `schemaVersion` is nonzero and `documentKey` is set.
- Equality compares all fields.
- A suggestion, preview session, and edit session must each carry one context version.

### `AI_OBJECT_REF`

Identifies a document object without exposing raw pointers to the AI layer.

Fields:

- `wxString documentKey`
- `KIID itemId`
- `KICAD_T itemType`
- `wxString sheetPath`
- `uint64_t generation`

Behavior:

- References may be resolved only by editor-specific adapters.
- Stale references must fail closed.
- The AI layer cannot store or call methods on raw `EDA_ITEM*`, `BOARD_ITEM*`, or `SCH_ITEM*`.

### `AI_SUGGESTION`

Represents a model or stub-provider proposal.

Fields:

- `KIID suggestionId`
- `AI_CONTEXT_VERSION contextVersion`
- `wxString title`
- `wxString explanation`
- `AI_SUGGESTION_KIND kind`
- `std::vector<AI_OBJECT_REF> targetRefs`
- `AI_PREVIEW_PLAN previewPlan`
- `AI_EDIT_PLAN editPlan`
- `AI_VALIDATION_SUMMARY validationSummary`

Behavior:

- Suggestions are inspectable and rejectable.
- A suggestion with no edit plan can still be a chat answer.
- A suggestion with an edit plan must be previewable or explicitly marked as no-preview.

### `AI_CANCELLATION_TOKEN`

Represents cancellation for model calls, preview building, and validation.

Behavior:

- User input, selection changes, active document changes, and explicit cancel can invalidate pending work.
- The runtime must not apply an edit after its token is cancelled.

## Runtime Responsibilities

The runtime coordinates, but does not own editor data.

Responsibilities:

- Create request IDs and suggestion IDs.
- Route user messages from Agent UI to an AI provider.
- Use a deterministic stub provider until production provider configuration exists.
- Store short-lived in-memory conversation state per editor frame.
- Receive context snapshots from editor adapters.
- Emit suggestion lifecycle events for trace.
- Refuse materialization requests when the context version is stale.

The runtime must not:

- Directly modify board or schematic objects.
- Own long-lived raw pointers to editor items.
- Decide that a DRC/ERC violation is acceptable without a policy from validation.
- Upload project data to a cloud provider by default.

## Provider Strategy

The first provider is `AI_STUB_PROVIDER`.

Stub behavior:

- Accepts a text message and context summary.
- Returns deterministic canned suggestions based on editor type.
- Can return a chat-only answer.
- Can return a no-op preview plan for UI testing.
- Never calls a network service.

Future provider behavior:

- OpenAI or other multimodal providers can be added behind `AI_PROVIDER`.
- Provider setup must go through a credential gate and explicit user configuration.
- Provider payload construction must respect privacy mode.

## Error Handling

Errors must be user-visible in Agent UI and trace-visible internally.

Error classes:

- Provider unavailable.
- Request cancelled.
- Context unavailable.
- Context stale.
- Preview unavailable.
- Edit plan rejected.
- Validation blocked.

No error may leave a preview item orphaned or a commit half-open.

## Testing Requirements

The implementation plan must use test-first development for:

- `AI_CONTEXT_VERSION::IsValid()`.
- `AI_OBJECT_REF` equality and stale generation handling.
- Stub provider deterministic response.
- Runtime cancellation before provider response.
- Runtime rejection of materialize when context version does not match.

Tests may start as unit tests over the foundation types and runtime with fake adapters. GUI tests are not required for this foundation spec.

## Acceptance Criteria

- Foundation headers and sources compile in the selected KiCad library target.
- Unit tests pass for core data types and stub provider behavior.
- No network access is required.
- No editor document can be mutated through the foundation runtime alone.
- The runtime exposes enough surface for the Agent window to send a message and receive a deterministic response.
