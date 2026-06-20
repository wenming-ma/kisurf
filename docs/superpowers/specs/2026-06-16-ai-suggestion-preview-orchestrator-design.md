# AI Suggestion Preview Orchestrator Design

Date: 2026-06-16

## Purpose

The first AI-native substrate can now capture context, visual snapshots, tool
catalogs, model tool calls, and recent editor activity. The next layer should turn
that sensed workspace state into reviewable suggestions without letting the model
mutate the project automatically.

This spec defines a native suggestion orchestrator: a bounded lifecycle for
candidate suggestions that can be triggered by recent activity, previewed in the
editor, accepted through existing edit-session boundaries, rejected by the user,
or expired when context changes.

## Source Research Anchors

Local source anchors:

- `include/kisurf/ai/ai_types.h` holds shared request, response, context,
  visual, action, validation, and activity records. Suggestion records should
  reuse these contracts rather than adding a parallel model.
- `include/kisurf/ai/ai_agent_panel_model.h` and
  `common/kisurf/ai/ai_agent_panel_model.cpp` already collect recent user
  activity and attach it to provider requests.
- `include/kisurf/ai/ai_preview_session.h` and
  `common/kisurf/ai/ai_preview_session.cpp` provide the current non-persistent
  preview boundary.
- `include/kisurf/ai/ai_edit_session.h` and
  `common/kisurf/ai/ai_edit_session.cpp` provide the accepted-edit boundary and
  validation gate.
- `include/kisurf/ai/ai_validation.h` and
  `common/kisurf/ai/ai_validation.cpp` provide blocking validation semantics.
- `include/kisurf/ai/ai_editor_activity_recorder.h` and
  `common/kisurf/ai/ai_editor_activity_recorder.cpp` map native tool events into
  high-signal activity records.

External practice anchors:

- VS Code's `InlineCompletionItemProvider` can be triggered either implicitly by
  typing or explicitly by user gesture, and providers receive a cancellation
  token:
  https://code.visualstudio.com/api/references/vscode-api
- The Language Server Protocol `CodeAction` model supports cheap initial action
  candidates and lazy `codeAction/resolve` for expensive edits:
  https://microsoft.github.io/language-server-protocol/specifications/lsp/3.17/specification/
- MCP tool guidance recommends confirmation for sensitive operations, showing
  tool inputs to the user, timeouts, validation, and audit logging:
  https://modelcontextprotocol.io/specification/2025-11-25/server/tools
- JetBrains inlay hints separate low-disruption inline/block presentation from
  the deeper action implementation:
  https://plugins.jetbrains.com/docs/intellij/inlay-hints.html

## Goals

- Add a native suggestion candidate data model with stable IDs, status, trigger
  metadata, explanation text, preview objects, edit objects, validation summary,
  and a compact fingerprint.
- Add an AI-agnostic suggestion provider interface that can be deterministic in
  tests and model-backed later.
- Add `AI_SUGGESTION_ORCHESTRATOR` to own the suggestion queue, assign IDs,
  deduplicate repeated triggers, bound memory, and drive preview/accept/reject
  state transitions.
- Reuse `AI_PREVIEW_SESSION` for non-persistent previews.
- Reuse `AI_EDIT_SESSION` for accepted materialization.
- Keep automatic background model calls out of the first implementation; the
  first orchestrator is synchronous and explicit so it is testable and safe.

## Non-Goals

- No autonomous routing or placement algorithm in this phase.
- No model call on every mouse move or every tool event.
- No automatic project mutation.
- No new canvas overlay renderer in this phase.
- No persistence of suggestion records across editor sessions.
- No IPC/MCP transport surface for suggestions yet.

## Design Decision

Use a native lifecycle orchestrator between sensing and editing:

1. Editor activity and context code build an `AI_SUGGESTION_TRIGGER`.
2. A suggestion provider may return one `AI_SUGGESTION_RECORD`.
3. The orchestrator assigns the record a native ID and sequence number.
4. Repeated triggers with the same fingerprint are ignored while an equivalent
   suggestion is pending or previewing.
5. The user or UI may request preview for a pending suggestion.
6. Preview uses `AI_PREVIEW_SESSION` and changes status to `Previewing`.
7. Accept uses `AI_EDIT_SESSION` and changes status to `Accepted` only when the
   edit session succeeds.
8. Reject changes status to `Rejected`; the UI or caller clears any active
   preview session it owns.
9. Context-version mismatch can expire pending suggestions before preview or
   accept.

This keeps model reasoning, visual preview, and document mutation as separate
steps. The model can propose; KiSurf owns lifecycle, policy, validation, and
commit semantics.

## Data Contract

Add suggestion status:

```cpp
enum class AI_SUGGESTION_STATUS
{
    Pending,
    Previewing,
    Accepted,
    Rejected,
    Expired
};
```

Add trigger:

```cpp
struct AI_SUGGESTION_TRIGGER
{
    AI_EDITOR_KIND      m_EditorKind = AI_EDITOR_KIND::Unknown;
    AI_CONTEXT_VERSION  m_ContextVersion;
    AI_CONTEXT_SNAPSHOT m_ContextSnapshot;
    AI_ACTIVITY_RECORD  m_Activity;
    wxString            m_Reason;
};
```

Add candidate:

```cpp
struct AI_SUGGESTION_RECORD
{
    uint64_t                      m_Id = 0;
    uint64_t                      m_Sequence = 0;
    AI_EDITOR_KIND                m_EditorKind = AI_EDITOR_KIND::Unknown;
    AI_SUGGESTION_KIND            m_Kind = AI_SUGGESTION_KIND::Preview;
    AI_SUGGESTION_STATUS          m_Status = AI_SUGGESTION_STATUS::Pending;
    AI_CONTEXT_VERSION            m_ContextVersion;
    uint64_t                      m_TriggerActivitySequence = 0;
    wxString                      m_Fingerprint;
    wxString                      m_Title;
    wxString                      m_Body;
    wxString                      m_ArgumentsJson;
    std::vector<AI_OBJECT_REF>    m_PreviewObjects;
    std::vector<AI_OBJECT_REF>    m_EditObjects;
    AI_VALIDATION_SUMMARY         m_Validation;
};
```

The fingerprint must not include raw project paths, clipboard contents, or large
serialized payloads. The first implementation should use editor kind, context
version, activity sequence, action name, and selected object labels.

## Orchestrator API

Create `include/kisurf/ai/ai_suggestion_orchestrator.h`:

```cpp
class KICOMMON_API AI_SUGGESTION_PROVIDER
{
public:
    virtual ~AI_SUGGESTION_PROVIDER() = default;

    virtual std::optional<AI_SUGGESTION_RECORD> Suggest(
            const AI_SUGGESTION_TRIGGER& aTrigger ) = 0;
};

class KICOMMON_API AI_SUGGESTION_ORCHESTRATOR
{
public:
    explicit AI_SUGGESTION_ORCHESTRATOR( AI_SUGGESTION_PROVIDER& aProvider,
                                         size_t aCapacity = 8 );

    std::optional<AI_SUGGESTION_RECORD> Update( AI_SUGGESTION_TRIGGER aTrigger );
    std::vector<AI_SUGGESTION_RECORD> Records() const;
    std::optional<AI_SUGGESTION_RECORD> Find( uint64_t aSuggestionId ) const;

    bool BeginPreview( uint64_t aSuggestionId, AI_PREVIEW_SESSION& aPreviewSession );
    bool Accept( uint64_t aSuggestionId, AI_EDIT_SESSION& aEditSession );
    bool Reject( uint64_t aSuggestionId );
    size_t ExpireStale( const AI_CONTEXT_VERSION& aCurrentVersion );
};
```

The API returns copies for now. The queue is small, so value semantics are simpler
and avoid exposing mutable internal state.

## Provider Boundary

The provider interface returns a candidate, not an edit committed to the design.
It can be implemented by:

- Deterministic unit-test providers.
- A future heuristic provider for common PCB/SCH workflows.
- A future model-backed provider that calls `AI_RUNTIME` with a suggestion prompt.

The first implementation should not wire the orchestrator to the default network
provider automatically. Any background model use needs a later policy spec with
debounce, cancellation, cost limits, and user-facing enablement.

## Lifecycle Rules

- `Update(...)` ignores triggers with unknown editor kind.
- `Update(...)` ignores triggers with no context and no activity.
- `Update(...)` asks the provider for a candidate only when the trigger is valid.
- Provider candidates without title/body and without preview/edit objects are
  ignored.
- If the provider leaves `m_Fingerprint` empty, the orchestrator computes one.
- Pending/previewing suggestions with the same fingerprint block duplicates.
- Accepted, rejected, and expired suggestions do not block future suggestions.
- Queue capacity evicts the oldest terminal records first. If capacity is still
  exceeded, evict the oldest record.
- `BeginPreview(...)` succeeds only for pending or previewing records with preview
  objects.
- `Accept(...)` succeeds only for pending or previewing records with edit objects,
  and only if `AI_EDIT_SESSION::Apply(...)` succeeds.
- `Reject(...)` succeeds for pending or previewing records and changes status to
  rejected. The caller remains responsible for clearing any visible preview.
- `ExpireStale(...)` expires pending or previewing records whose context version
  differs from the current context version.

## UI And Editor Integration

The first implementation should be headless and common-library only. A later UI
slice can connect it to the Agent panel and canvas:

- Show one compact pending suggestion in the Agent pane.
- Let the user preview, accept, or reject.
- Clear preview on reject, accept, context expiration, or pane destruction.

This keeps the lifecycle testable before adding interaction chrome.

## IPC Position

IPC can later expose read-only suggestion records or explicit accept/reject
commands. It should not own suggestion generation or lifecycle state in this
phase. The native orchestrator is closer to context, preview, validation, and
commit boundaries.

## Test Strategy

- Unit-test that valid triggers ask the provider and store one pending suggestion.
- Unit-test duplicate fingerprint suppression.
- Unit-test bounded queue eviction.
- Unit-test preview transition through a fake `AI_PREVIEW_ADAPTER`.
- Unit-test accept transition through a fake `AI_EDIT_ADAPTER`.
- Unit-test blocking validation prevents accepted status.
- Unit-test reject and stale-context expiration.
- Run `qa_common` targeted tests.

## Acceptance Criteria

- Suggestions have a native lifecycle independent of provider implementation.
- No suggestion is materialized without `AI_EDIT_SESSION`.
- Duplicate recent-activity triggers do not spam the queue.
- Preview and accept paths are separately testable.
- The first implementation does not add background model calls.
- The design remains compatible with future PCB placement and routing preview
  providers.

## Risks And Mitigations

- **Suggestion spam:** fingerprint de-duplication and small queue capacity.
- **Stale context:** explicit context-version expiration before preview or accept.
- **Accidental mutation:** accept only through `AI_EDIT_SESSION`.
- **Prompt/cost runaway:** no automatic background model call in this phase.
- **Weak first previews:** object-ref previews are limited but establish lifecycle
  before geometry/routing providers are added.

## Spec Self-Review

- Placeholder scan: no placeholder or fill-in sections remain.
- Consistency check: lifecycle, data contract, and API all use pending,
  previewing, accepted, rejected, and expired states.
- Scope check: this is a common-library lifecycle substrate, not a UI or routing
  implementation.
- Ambiguity check: automatic background model calls and IPC surfaces are explicit
  non-goals for this phase.
