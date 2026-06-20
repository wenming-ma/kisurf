# AI-Native Preview-First Tool Stack Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Establish the shared preview-first tool stack used by both the Chat Agent and the G2R Preview Agent.

**Architecture:** Introduce a typed operation-bundle layer beneath suggestions. Agents and scripts create operation bundles; editor integrations preview, accept, reject, log, and commit bundles.

**Tech Stack:** C++20, wxWidgets, nlohmann::json, KiCad GAL view preview APIs, KiCad undo/commit infrastructure, Boost.Test.

---

## File Structure

- Create: `include/kisurf/ai/ai_operation_bundle.h` for atomic operation ids,
  operation arguments, bundle metadata, source workflow, and preview policy.
- Create: `common/kisurf/ai/ai_operation_bundle.cpp` for JSON projection,
  validation helpers, and fingerprints.
- Create: `include/kisurf/ai/ai_operation_registry.h` for operation descriptors
  and lookup.
- Create: `common/kisurf/ai/ai_operation_registry.cpp` for base registry
  implementation.
- Modify: `include/kisurf/ai/ai_suggestion_orchestrator.h` and
  `common/kisurf/ai/ai_suggestion_orchestrator.cpp` to carry operation bundles
  inside preview suggestions.
- Modify: `common/kisurf/ai/ai_semantic_tool_call_handler.cpp` to create bundles
  instead of ad hoc preview objects for new tools.
- Modify: `pcbnew/kisurf_ai_pcb_preview_adapter.*` and
  `pcbnew/kisurf_ai_pcb_operation_edit_adapter.*` to preview and accept bundles.
- Modify: `pcbnew/pcb_edit_frame.cpp` to keep preview ownership in editor
  callbacks and later wire Tab/Esc.
- Modify: `qa/tests/common/*ai*` and `qa/tests/pcbnew/*ai*` for TDD coverage.

### Task 1: Operation Bundle Core

**Files:**
- Create: `include/kisurf/ai/ai_operation_bundle.h`
- Create: `common/kisurf/ai/ai_operation_bundle.cpp`
- Test: `qa/tests/common/test_ai_operation_bundle.cpp`

- [ ] **Step 1: Write failing tests**

Add tests for:

```cpp
AI_OPERATION_BUNDLE bundle;
bundle.m_SourceWorkflow = AI_OPERATION_SOURCE_WORKFLOW::Chat;
bundle.m_EditorKind = AI_EDITOR_KIND::Pcb;
bundle.m_Operations.push_back( AI_OPERATION::Via( ... ) );
BOOST_CHECK( bundle.RequiresPreview() );
BOOST_CHECK( bundle.Fingerprint().Contains( wxS( "pcb|chat|via" ) ) );
```

Expected RED: types do not exist.

- [ ] **Step 2: Implement minimal bundle types**

Define:

```cpp
enum class AI_OPERATION_SOURCE_WORKFLOW { Chat, Background };
enum class AI_OPERATION_KIND { CreateVia, CreateTrackSegment, CreateShape, CreateZone, MoveObjects, SetPanelCell };
struct AI_OPERATION { AI_OPERATION_KIND m_Kind; wxString m_ArgumentsJson; };
struct AI_OPERATION_BUNDLE { ... };
```

Keep JSON fields generic in this task.

- [ ] **Step 3: Verify**

Build `qa_common` and run `AiOperationBundle`.

### Task 2: Operation Registry

**Files:**
- Create: `include/kisurf/ai/ai_operation_registry.h`
- Create: `common/kisurf/ai/ai_operation_registry.cpp`
- Test: `qa/tests/common/test_ai_operation_registry.cpp`

- [ ] **Step 1: Write failing tests**

Assert registry can describe `pcb.create_via`, `pcb.create_track_segment`,
`pcb.create_shape`, `pcb.create_zone`, `pcb.move_objects`, and
`ui.set_panel_cell`, each with preview-required policy.

- [ ] **Step 2: Implement descriptors**

Add descriptor fields:

```cpp
wxString m_Id;
AI_EDITOR_KIND m_EditorKind;
AI_OPERATION_KIND m_Kind;
bool m_RequiresPreview = true;
bool m_Scriptable = true;
```

- [ ] **Step 3: Verify**

Run registry tests.

### Task 3: PCB Via Bundle Adapter

**Files:**
- Modify: `pcbnew/kisurf_ai_pcb_preview_adapter.*`
- Modify: `pcbnew/kisurf_ai_pcb_operation_edit_adapter.*`
- Test: `qa/tests/pcbnew/test_ai_pcb_preview_adapter.cpp`
- Test: `qa/tests/pcbnew/test_ai_pcb_operation_edit_adapter.cpp`

- [ ] **Step 1: Write failing tests**

Create an operation bundle with one `pcb.create_via` operation and assert preview
creates an owned `PCB_VIA` without mutating the board. Assert accept commits one
via through one commit.

- [ ] **Step 2: Implement via bundle mapping**

Map `pcb.create_via` to the existing `place_via_preview` parser/creation path
where possible to avoid duplicate geometry code.

- [ ] **Step 3: Verify**

Run PCB preview and operation edit adapter tests.

### Task 4: Semantic Tool Creates Bundles

**Files:**
- Modify: `common/kisurf/ai/ai_semantic_tool_call_handler.cpp`
- Modify: `common/kisurf/ai/ai_provider.cpp`
- Test: `qa/tests/common/test_ai_semantic_tool_call_handler.cpp`
- Test: `qa/tests/common/test_ai_provider.cpp`

- [ ] **Step 1: Write failing tests**

Add `kisurf_preview_place_via` schema and handler tests. Assert the result is a
preview suggestion whose operation bundle contains `pcb.create_via`.

- [ ] **Step 2: Implement tool schema and handler**

Expose typed arguments for via creation and store a bundle-backed preview
suggestion.

- [ ] **Step 3: Verify**

Run `AiProvider` and `AiSemanticToolCallHandler`.

### Task 5: Workspace View Dynamic Context Options

**Files:**
- Modify: `common/kisurf/ai/ai_semantic_tool_call_handler.cpp`
- Modify: `common/kisurf/ai/ai_provider.cpp`
- Test: `qa/tests/common/test_ai_semantic_tool_call_handler.cpp`

- [ ] **Step 1: Write failing tests**

Add `workspace_view.dynamic_context` options for routing/layout/general/panel and
visual options for anchors and current-net highlighting.

- [ ] **Step 2: Implement bounded JSON projection**

Return active context, tool state, route anchors, placement anchors, pattern
anchors, panel focus, and visual snapshot metadata.

- [ ] **Step 3: Verify**

Run semantic tool-call tests.

### Task 6: Script Bundle Prototype

**Files:**
- Create: `include/kisurf/ai/ai_script_bundle.h`
- Create: `common/kisurf/ai/ai_script_bundle.cpp`
- Test: `qa/tests/common/test_ai_script_bundle.cpp`

- [ ] **Step 1: Write failing tests**

Assert a bounded script can emit multiple `pcb.create_via` operations into one
preview bundle and refuses over-limit operation counts.

- [ ] **Step 2: Implement in-process declarative script format**

Start with JSON script plans, not arbitrary code execution:

```json
{ "operations": [ { "op": "pcb.create_via", "args": { ... } } ] }
```

- [ ] **Step 3: Verify**

Run script bundle tests.

### Task 7: Editor Interaction Hooks

**Files:**
- Modify: `pcbnew/pcb_edit_frame.cpp`
- Test: focused PCB/editor tests where practical.

- [ ] **Step 1: Define accept/cancel contract**

Tab accepts the active background preview. Esc or workspace click rejects/cancels
the active background preview.

- [ ] **Step 2: Implement behind feature flag or existing background-agent toggle**

Only active when background agent is enabled and an editor-owned preview is
active.

- [ ] **Step 3: Verify with Computer Use**

Build `pcbnew`, launch it, generate or load a test preview path, press Tab/Esc,
and observe no runtime popup.

### Task 8: Final Verification

- [ ] Run full `qa_common.exe --run_test=Ai* --report_level=short`.
- [ ] Run focused PCB AI tests.
- [ ] Build `pcbnew`.
- [ ] Launch with Computer Use and inspect for missing DLL/system popups.
- [ ] Run secret scan and `git diff --check`.
- [ ] Commit each completed slice with a focused message.

## Self-Review

- Spec coverage: operation bundles, registry, atomic tools, composite tools,
  scripts, workspace view, anchors, and two workflows are covered.
- Marker scan: no incomplete marker instructions remain.
- Type consistency: operation bundle names are consistent across planned files
  and tests.
