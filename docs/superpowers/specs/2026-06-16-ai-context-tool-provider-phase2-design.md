# KiSurf AI Native Phase 2: Context, Tools, Provider

Date: 2026-06-16

## Decision

Phase 2 extends the completed first slice from a chat-capable native pane into a model-consumable editor substrate. The implementation remains Native First and IPC Compatible: native editor state is the source of truth, and IPC can later expose projections of the same contracts.

The user has authorized continuing without further blocking questions. Provider credentials are treated as runtime-only configuration: KiSurf reads `OPENAI_API_KEY` from the user/process environment, uses `https://sub2api.wenming-dev.org/v1` as the default OpenAI-compatible base URL, and never stores or logs the key.

## Goals

1. Give every provider request a structured editor context package.
2. Give the model a machine-readable catalog of available editor actions.
3. Add an action/tool trace model that can later record user actions and model tool calls.
4. Add an OpenAI-compatible provider seam that can use the supplied base URL and runtime environment key.
5. Keep all tests offline by default. Live API smoke tests are allowed only when the running environment already exposes a usable key.

## Non-Goals

- No autonomous placement/routing algorithm in this phase.
- No accepted model edit may bypass existing preview, commit, undo, validation, or user-acceptance seams.
- No bitmap upload is required in this phase. A visual snapshot placeholder is part of the context contract so the later canvas-memory capture path has a stable home.
- No secret may be persisted to the repository, test logs, trace logs, or saved project files.

## Context Package

Add `AI_CONTEXT_SNAPSHOT` as the model-facing context object:

- `m_EditorKind`
- `m_Version`
- `m_VisibleObjects`
- `m_SelectedObjects`
- `m_Actions`
- `m_Visual`
- `m_Summary`

`AI_CONTEXT_INDEX` remains the editor-side incremental index. It builds snapshots but does not know about provider transport. PCB and schematic adapters continue to populate visible and selected native object references.

`AI_VISUAL_SNAPSHOT` is intentionally conservative in this phase. It records source, MIME type, and optional data URI. Canvas-memory bitmap capture is deferred, but every provider request already has a place to carry it.

## Tool Catalog

Add `AI_ACTION_DESCRIPTOR` and `AI_ACTION_CATALOG`.

The descriptor contains:

- stable action name, such as `common.Control.showAgentPanel`
- friendly name
- description or tooltip
- editor kind
- action safety classification
- enabled state

The initial catalog is discoverable and read-only from the model's perspective. It lists actions but does not let a model execute them directly. Future tool invocation must pass through allowlist, current-editor checks, interaction safety, preview/commit policy, and audit logging.

Safety classes:

- `ReadOnly`: view, search, inspect, zoom, panel toggles.
- `Interactive`: starts or changes an interactive tool without directly changing the design.
- `Modifying`: can change board/schematic data.
- `Destructive`: delete/revert/overwrite-style operations.

Unknown actions default to `Interactive`, not `ReadOnly`.

## Provider Request

Extend `AI_PROVIDER_REQUEST` with:

- `m_ContextSnapshot`
- future-compatible tool call fields via trace records, not direct editor mutation

The Agent panel must send the current context snapshot with user text. Stub provider output should surface enough context metadata to verify the connection without requiring a network call.

## OpenAI-Compatible Provider

Add a provider implementation with a narrow, testable HTTP seam.

Runtime configuration:

- Base URL: `KISURF_AI_BASE_URL`, then `OPENAI_BASE_URL`, then `https://sub2api.wenming-dev.org/v1`.
- API key: `OPENAI_API_KEY`.
- Model: `KISURF_AI_MODEL`, then `OPENAI_MODEL`, then a conservative default.
- Provider mode: `KISURF_AI_PROVIDER=stub` forces stub, otherwise a present API key enables the OpenAI-compatible provider.

Transport:

- POST to `/chat/completions`.
- Include context snapshot as text in the user message for this phase.
- Do not print, serialize, or expose the authorization header except inside the HTTP request.
- Return a normal chat response when the provider returns `choices[0].message.content`.
- Return a user-visible configuration/network error response without exposing secrets when missing key, HTTP failure, invalid JSON, or empty content occurs.

## Action Trace

Add `AI_TOOL_CALL_RECORD` or equivalent trace-ready data to the shared AI types. In this phase it records provider-visible request/response context and can hold future tool call/result entries. Direct execution remains out of scope until the allowlist and undo/preview policies are implemented.

## Acceptance Criteria

- Unit tests prove context snapshots include visible objects, selected objects, visual placeholder state, and actions.
- Unit tests prove the action catalog can describe KiCad `TOOL_ACTION` objects and classify common actions safely.
- Unit tests prove the Agent panel model sends context to providers.
- Unit tests prove default provider configuration reads only environment metadata and never needs plaintext inspection in test output.
- Unit tests prove the OpenAI-compatible provider builds the correct URL, method, JSON content shape, and authorization header through a fake HTTP handler.
- PCB and schematic Agent panels pass context snapshots from their native adapters.
- Full local verification builds `qa_common`, `qa_pcbnew`, `qa_eeschema`, `pcbnew_kiface`, `eeschema_kiface`, and `kicad`.

## Deferred Follow-Up

Phase 2B should add model tool invocation:

- allowlisted tool calls
- `TOOL_MANAGER::RunAction` bridge
- user-action recording
- model-action recording
- preview/materialize policy checks
- action replay/audit UI

Phase 2C should add native visual capture:

- canvas-memory snapshot abstraction
- size-bounded image payloads
- explicit privacy policy
- multimodal provider request serialization

