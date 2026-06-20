# AI Anchor Focus PCB Preview Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make `anchor_focus_preview` operation-only suggestions render a non-mutating PCB canvas preview marker.

**Architecture:** Add a default no-op operation hook to the common preview-session adapter contract, dispatch parsed operations from the suggestion orchestrator, and override the hook in the PCB preview adapter for anchor-focus operations. The PCB adapter renders two synthetic `PCB_SHAPE` segments as a crosshair marker owned by the preview view.

**Tech Stack:** C++17, wxWidgets, KiCad PCB data model, Boost unit tests, CMake/Ninja.

---

## File Map

- Modify `include/kisurf/ai/ai_preview_session.h`: forward-declare `AI_SUGGESTION_OPERATION`, add adapter/session operation hook.
- Modify `common/kisurf/ai/ai_preview_session.cpp`: implement default no-op adapter method and `AI_PREVIEW_SESSION::ShowOperation`.
- Modify `common/kisurf/ai/ai_suggestion_orchestrator.cpp`: parse and dispatch preview operations in `BeginPreview`.
- Modify `pcbnew/kisurf_ai_pcb_preview_adapter.h`: override `ShowOperation`.
- Modify `pcbnew/kisurf_ai_pcb_preview_adapter.cpp`: build anchor-focus marker shapes.
- Modify `qa/tests/common/test_ai_preview_session.cpp`: test operation hook dispatch.
- Modify `qa/tests/common/test_ai_suggestion_orchestrator.cpp`: test operation-only suggestion preview dispatch.
- Modify `qa/tests/common/test_ai_agent_panel_model.cpp`: test model path dispatches anchor-focus operations.
- Modify `qa/tests/pcbnew/test_ai_pcb_preview_adapter.cpp`: test PCB marker rendering and stale ids.
- Modify `README.md`: mention anchor-focus operation previews render PCB canvas markers.

## Task 1: Common Preview Operation Red Tests

- [ ] **Step 1: Add preview-session test**

Add a fake adapter override that records `ShowOperation` calls. Create an `AI_SUGGESTION_OPERATION` with `m_Kind = AnchorFocusPreview`, call `AI_PREVIEW_SESSION::ShowOperation`, and assert events are `begin:1` then `operation:1:anchor-id`.

- [ ] **Step 2: Add orchestrator/model tests**

Update fake preview adapters in orchestrator/model tests to record operation anchor ids. Preview an operation-only anchor-focus suggestion and assert one operation was shown, no objects were shown, and status becomes `Previewing`.

- [ ] **Step 3: Verify red**

Run:

```powershell
qa_common.exe --run_test=AiPreviewSession,AiSuggestionOrchestrator,AiAgentPanelModel
```

Expected before implementation: compile fails because `ShowOperation` does not exist.

## Task 2: Common Preview Operation Hook

- [ ] **Step 1: Add preview contract**

Add `struct AI_SUGGESTION_OPERATION;` to `ai_preview_session.h`. Add `AI_PREVIEW_ADAPTER::ShowOperation(...)` with a default implementation, and `AI_PREVIEW_SESSION::ShowOperation(...)`.

- [ ] **Step 2: Dispatch operations**

In `AI_SUGGESTION_ORCHESTRATOR::BeginPreview`, parse `record->m_ArgumentsJson`; when present, call `aPreviewSession.ShowOperation( *operation )` before showing preview objects.

- [ ] **Step 3: Verify green**

Run:

```powershell
qa_common.exe --run_test=AiPreviewSession,AiSuggestionOrchestrator,AiAgentPanelModel
```

Expected: targeted common tests pass.

## Task 3: PCB Anchor Marker Red Test

- [ ] **Step 1: Add PCB adapter marker tests**

Add tests that call `adapter.BeginPreview( 42 )` and `adapter.ShowOperation( 42, anchorFocusOperation )`. Assert two previewed items exist, both are `PCB_SHAPE_T`, both are on `F_Cu`, their endpoints cross through the anchor center, and `fixture.m_Board.Drawings().empty()` remains true. Add a stale-id test that calls `ShowOperation( 7, operation )` after `BeginPreview( 42 )` and asserts no preview items.

- [ ] **Step 2: Verify red**

Run:

```powershell
cmake --build out\build\x64-release --target qa_pcbnew --config Release
qa_pcbnew.exe --run_test=AiPcbPreviewAdapter
```

Expected before implementation: compile fails because the PCB adapter has no `ShowOperation` override.

## Task 4: PCB Anchor Marker Implementation

- [ ] **Step 1: Build marker helper**

In `kisurf_ai_pcb_preview_adapter.cpp`, include `pcb_shape.h`. Add a helper that creates two `PCB_SHAPE` `SHAPE_T::SEGMENT` items centered at `operation.m_Position`, sets layer to resolved `operation.m_FocusLayer` or `F_Cu`, and sets a fixed width.

- [ ] **Step 2: Implement override**

Add `KISURF_AI_PCB_PREVIEW_ADAPTER::ShowOperation(...)`. Ignore stale ids and non-anchor-focus operations; add both marker shapes to preview with ownership and append them to `m_PreviewedItems`.

- [ ] **Step 3: Verify green**

Run:

```powershell
qa_pcbnew.exe --run_test=AiPcbPreviewAdapter
```

Expected: PCB preview adapter tests pass.

## Task 5: Full Verification and Commit

- [ ] **Step 1: Update README**

Add one line to the direct-use status noting that anchor-focus operation previews now render lightweight PCB canvas markers.

- [ ] **Step 2: Run verification**

Run:

```powershell
cmake --build out\build\x64-release --target qa_common --config Release
qa_common.exe --run_test=AiPreviewSession,AiSuggestionOrchestrator,AiAgentPanelModel
cmake --build out\build\x64-release --target qa_pcbnew --config Release
qa_pcbnew.exe --run_test=AiPcbPreviewAdapter
git diff --check
$prefix = 'sk' + '-'
$pattern = $prefix + '[A-Za-z0-9_-]{20,}|OPENAI_API_KEY=.*' + $prefix
rg -n $pattern common docs include qa pcbnew README.md
```

Expected: builds/tests pass, diff check has no errors, secret scan has no matches.

- [ ] **Step 3: Commit**

Stage only this slice and commit:

```powershell
git add docs/superpowers/specs/2026-06-19-ai-anchor-focus-pcb-preview-design.md docs/superpowers/plans/2026-06-19-ai-anchor-focus-pcb-preview-implementation.md include/kisurf/ai/ai_preview_session.h common/kisurf/ai/ai_preview_session.cpp common/kisurf/ai/ai_suggestion_orchestrator.cpp pcbnew/kisurf_ai_pcb_preview_adapter.h pcbnew/kisurf_ai_pcb_preview_adapter.cpp qa/tests/common/test_ai_preview_session.cpp qa/tests/common/test_ai_suggestion_orchestrator.cpp qa/tests/common/test_ai_agent_panel_model.cpp qa/tests/pcbnew/test_ai_pcb_preview_adapter.cpp README.md
git commit -m "feat: render anchor focus PCB previews"
```

Expected: commit succeeds; unrelated `qa/tests/pcbnew/test_module.cpp` remains unstaged.
