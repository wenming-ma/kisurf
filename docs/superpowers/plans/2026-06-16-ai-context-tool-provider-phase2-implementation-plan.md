# AI Context Tool Provider Phase 2 Implementation Plan

Date: 2026-06-16

Spec: `docs/superpowers/specs/2026-06-16-ai-context-tool-provider-phase2-design.md`

## Scope

Implement the next native AI substrate slice:

- model-facing context snapshots
- model-facing action catalog discovery
- runtime-only OpenAI-compatible provider configuration
- offline-testable OpenAI-compatible provider
- PCB and schematic Agent panel context wiring

## Steps

1. Add failing common tests for context snapshots and action descriptors.
2. Implement shared AI types and `AI_CONTEXT_INDEX` snapshot building.
3. Add failing tests for action catalog construction from `TOOL_ACTION`.
4. Implement `AI_ACTION_CATALOG` in the common layer, not `kicommon`, because it depends on the tool/action subsystem.
5. Add failing tests that `AI_AGENT_PANEL_MODEL` passes context snapshots into provider requests.
6. Extend the Agent panel/model to accept a context provider callback.
7. Add failing tests for provider environment settings and fake HTTP transport.
8. Implement `AI_OPENAI_COMPAT_PROVIDER`, `AI_PROVIDER_SETTINGS`, and default provider selection.
9. Wire PCB and schematic Agent panels to native context adapters plus action catalog snapshots.
10. Run targeted tests, then full build targets.

## Verification

Targeted:

- `qa_common --run_test=AiNativeTypes`
- `qa_common --run_test=AiNativeProvider`
- `qa_common --run_test=AiAgentPanelModel`
- `qa_common --run_test=AiActionCatalog`
- `qa_pcbnew --run_test=AiPcbContextAdapter`
- `qa_eeschema --run_test=AiSchContextAdapter`

Full:

- build `qa_common qa_pcbnew qa_eeschema pcbnew_kiface eeschema_kiface kicad`

Live provider:

- skipped unless the process environment exposes `OPENAI_API_KEY`
- no command prints the key

