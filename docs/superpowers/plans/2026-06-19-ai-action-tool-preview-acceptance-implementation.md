# AI Action Tool Preview Acceptance Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make `kisurf_run_action` produce a pending preview suggestion and run
the action only after user Accept.

**Architecture:** Keep action policy validation in `AI_ACTION_TOOL_CALL_HANDLER`,
store an action-preview suggestion through the Agent model, and materialize the
action in `AI_AGENT_PANEL::AcceptLatestSuggestion()` through the installed
editor action runner.

**Tech Stack:** C++20, wxWidgets, nlohmann::json, Boost.Test, KiCad action runner
integration.

---

Date: 2026-06-19
Spec: `docs/superpowers/specs/2026-06-19-ai-action-tool-preview-acceptance-design.md`

## Files

- Modify: `include/kisurf/ai/ai_action_tool_call_handler.h`
- Modify: `common/kisurf/ai/ai_action_tool_call_handler.cpp`
- Modify: `include/kisurf/ai/ai_suggestion_orchestrator.h`
- Modify: `common/kisurf/ai/ai_suggestion_orchestrator.cpp`
- Modify: `include/kisurf/ai/ai_agent_panel_model.h`
- Modify: `common/kisurf/ai/ai_agent_panel_model.cpp`
- Modify: `include/kisurf/ai/ai_agent_panel.h`
- Modify: `common/kisurf/ai/ai_agent_panel.cpp`
- Modify: `qa/tests/common/test_ai_action_tool_call_handler.cpp`
- Modify: `qa/tests/common/test_ai_suggestion_orchestrator.cpp`
- Modify: `qa/tests/common/test_ai_agent_panel_model.cpp`

## Completed Tasks

- [x] Write failing tests for `kisurf_run_action` creating an action-preview
  suggestion without calling the action runner.
- [x] Write failing tests for marking an action-preview suggestion accepted
  without edit objects.
- [x] Add an optional suggestion sink to `AI_ACTION_TOOL_CALL_HANDLER`.
- [x] Force `kisurf_run_action` through dry-run validation regardless of model
  `dry_run` input.
- [x] Create action-preview suggestion records with `operation:"action_preview"`.
- [x] Return `preview_ready` result JSON with `preview_required:true` and
  `suggestion_id`.
- [x] Add orchestrator/model APIs for marking a non-edit action preview as
  accepted.
- [x] Route `AI_AGENT_PANEL::AcceptLatestSuggestion()` through the installed
  action runner when the active suggestion is an action preview.
- [x] Keep `AI_EDIT_SESSION` acceptance false for suggestions with no edit
  objects.
- [x] Verify common and PCB AI tests.
- [x] Verify build-tree PCB Editor launch with Computer Use and no system-error
  modal.

## Verification Evidence

- `cmake --build out\build\x64-release --target qa_common qa_pcbnew --config Release`
  exited 0.
- `qa_common.exe --run_test=*Ai* --catch_system_errors=no --report_level=short`
  exited 0 with 320 AI test cases and 2248 assertions passing.
- `qa_pcbnew.exe --run_test=*Ai* --catch_system_errors=no --report_level=short`
  exited 0 with 62 AI test cases and 533 assertions passing.
- Computer Use captured the build-tree PCB Editor window from
  `out\build\x64-release\pcbnew\pcbnew.exe`; no missing-DLL/system-error modal
  was present.
- Commit: `8528800a ai: queue action previews for acceptance`.

## Self-Review

- Spec coverage: every requirement maps to the completed tests and code paths
  above.
- Marker scan: no incomplete marker text remains.
- Type consistency: action-preview markers consistently use
  `operation:"action_preview"` and `suggestion_id`.
- Scope check: this slice changes action preview/accept only; it does not add
  new catalog actions or broader autonomous behavior.
