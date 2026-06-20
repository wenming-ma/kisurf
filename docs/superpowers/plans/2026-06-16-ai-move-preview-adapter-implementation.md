# AI Move Preview Adapter Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Show moved clones during AI suggestion preview when a bounded move
delta is supplied, while preserving existing highlight-only preview and accept
commit behavior.

**Architecture:** Extend PCB and schematic preview adapters with an optional
`VECTOR2I` delta. `ShowObject(...)` resolves the original object as today. With
no delta, it previews the original pointer without ownership. With a delta, it
clones the object, moves the clone, adds it to the preview group with ownership,
and records the clone pointer for tests.

**Tech Stack:** C++17, KiSurf PCB/SCH preview adapters, KIGFX::VIEW preview
group, Boost.Test.

---

## File Structure

- Modify: `pcbnew/kisurf_ai_pcb_preview_adapter.h`
- Modify: `pcbnew/kisurf_ai_pcb_preview_adapter.cpp`
- Modify: `eeschema/kisurf_ai_sch_preview_adapter.h`
- Modify: `eeschema/kisurf_ai_sch_preview_adapter.cpp`
- Modify: `pcbnew/pcb_edit_frame.cpp`
- Modify: `eeschema/sch_edit_frame.cpp`
- Modify: `qa/tests/pcbnew/test_ai_pcb_preview_adapter.cpp`
- Modify: `qa/tests/eeschema/test_ai_sch_preview_adapter.cpp`
- Modify: `docs/superpowers/specs/2026-06-16-kisurf-ai-native-spec-index.md`

## Verification Commands

Preview and edit verification:

```bat
cmd.exe /d /s /c """C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat"" -arch=x64 -host_arch=x64 >nul && cmake --build out/build/x64-release --target qa_pcbnew qa_eeschema -- -j 2 && set KICAD_RUN_FROM_BUILD_DIR=1 && set KICAD_BUILD_PATHS=C:/Users/wenming/source/repos/kisurf/out/build/x64-release/kicad:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/common:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/api:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/common/gal:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/pcbnew:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/eeschema:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/cvpcb && set PATH=D:\Tools\vcpkg\installed\x64-windows\bin;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\kicad;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\common;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\api;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\common\gal;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\pcbnew;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\eeschema;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\cvpcb;%PATH% && out\build\x64-release\qa\tests\pcbnew\qa_pcbnew.exe --run_test=AiPcbPreviewAdapter,AiPcbMoveEditAdapter --log_level=test_suite && out\build\x64-release\qa\tests\eeschema\qa_eeschema.exe --run_test=AiSchPreviewAdapter,AiSchMoveEditAdapter --log_level=test_suite
```

## Task 1: RED Preview Tests

**Files:**
- Modify: `qa/tests/pcbnew/test_ai_pcb_preview_adapter.cpp`
- Modify: `qa/tests/eeschema/test_ai_sch_preview_adapter.cpp`

- [x] **Step 1: Set fixture object positions**

Give the preview fixture objects non-zero positions so moved clone assertions are
meaningful.

- [x] **Step 2: Add PCB moved clone preview test**

Create `MovePreviewShowsMovedCloneWithoutChangingOriginal`. Construct the PCB
preview adapter with `VECTOR2I( 100, -25 )`, preview `U1.1`, assert the tracked
preview item is not the original pad, the original position is unchanged, and
the preview item position is original plus delta.

- [x] **Step 3: Add schematic moved clone preview test**

Create the equivalent schematic test for symbol `R1`.

- [x] **Step 4: Run RED verification**

Run the preview and edit verification command.

Expected: compile failures because the preview adapters do not accept a move
delta yet.

## Task 2: Adapter Implementation

**Files:**
- Modify: `pcbnew/kisurf_ai_pcb_preview_adapter.h`
- Modify: `pcbnew/kisurf_ai_pcb_preview_adapter.cpp`
- Modify: `eeschema/kisurf_ai_sch_preview_adapter.h`
- Modify: `eeschema/kisurf_ai_sch_preview_adapter.cpp`

- [x] **Step 1: Add optional delta member**

Include `math/vector2d.h` and `<optional>`, add `std::optional<VECTOR2I>
m_MoveDelta`, and extend constructors with an optional delta parameter defaulted
to `std::nullopt`.

- [x] **Step 2: Clone and move for PCB previews**

In PCB `ShowObject(...)`, when `m_MoveDelta` is set, clone the resolved
`BOARD_ITEM`, move the clone, add it to preview with ownership, track the clone,
and return.

- [x] **Step 3: Clone and move for schematic previews**

Apply the same behavior to `SCH_ITEM` clones.

- [x] **Step 4: Run GREEN verification**

Run the preview and edit verification command.

Expected: PCB/SCH preview and move-edit suites pass.

- [x] **Step 5: Wire frame preview handlers**

Update PCB and schematic suggestion preview handlers to parse move arguments
from the current suggestion and pass the optional delta to the preview adapter.

## Task 3: Index, Hygiene, And Commit

**Files:**
- Modify: `docs/superpowers/specs/2026-06-16-kisurf-ai-native-spec-index.md`

- [x] **Step 1: Update spec index**

Add:

```markdown
23. [AI Move Preview Adapter](./2026-06-16-ai-move-preview-adapter-design.md)
   - Defines clone-based move previews for accepted suggestion move arguments.
```

Add implementation order:

```markdown
27. Phase 20 clone-based move preview adapters for PCB and schematic suggestions.
```

- [x] **Step 2: Run diff hygiene checks**

Run:

```powershell
git diff --check
git status --short
git diff --stat
```

- [x] **Step 3: Commit**

```powershell
git add pcbnew/kisurf_ai_pcb_preview_adapter.h pcbnew/kisurf_ai_pcb_preview_adapter.cpp pcbnew/pcb_edit_frame.cpp eeschema/kisurf_ai_sch_preview_adapter.h eeschema/kisurf_ai_sch_preview_adapter.cpp eeschema/sch_edit_frame.cpp qa/tests/pcbnew/test_ai_pcb_preview_adapter.cpp qa/tests/eeschema/test_ai_sch_preview_adapter.cpp docs/superpowers/specs/2026-06-16-ai-move-preview-adapter-design.md docs/superpowers/specs/2026-06-16-kisurf-ai-native-spec-index.md docs/superpowers/plans/2026-06-16-ai-move-preview-adapter-implementation.md
git commit -m "feat: preview ai move suggestions"
```

## Plan Self-Review

- Spec coverage: tasks cover default preview preservation, moved clone preview,
  original-object immutability, and verification.
- Open-marker scan: no unresolved open markers remain.
- Safety check: preview clones are owned by the existing view preview lifecycle.
