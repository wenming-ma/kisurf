# AI Native Preview Adapter Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add PCB and schematic `AI_PREVIEW_ADAPTER` implementations that show
resolved AI object references through native `KIGFX::VIEW` preview groups.

**Architecture:** Reuse Phase 9 object resolvers and KiCad's existing
`VIEW::AddToPreview(..., false)` / `VIEW::ClearPreview()` lifecycle. Keep the
common AI layer unchanged and keep preview adapters editor-local.

**Tech Stack:** C++20, Boost.Test, KiCad `KIGFX::VIEW`, `qa_pcbnew`,
`qa_eeschema`, `qa_common`.

---

## File Structure

- Create: `pcbnew/kisurf_ai_pcb_preview_adapter.h`
  - PCB adapter implementing `AI_PREVIEW_ADAPTER`.
- Create: `pcbnew/kisurf_ai_pcb_preview_adapter.cpp`
  - Resolves PCB references and adds `BOARD_ITEM*` values to `KIGFX::VIEW`.
- Modify: `pcbnew/CMakeLists.txt`
  - Add `kisurf_ai_pcb_preview_adapter.cpp`.
- Create: `qa/tests/pcbnew/test_ai_pcb_preview_adapter.cpp`
  - PCB preview adapter tests.
- Modify: `qa/tests/pcbnew/CMakeLists.txt`
  - Add PCB preview test.
- Create: `eeschema/kisurf_ai_sch_preview_adapter.h`
  - Schematic adapter implementing `AI_PREVIEW_ADAPTER`.
- Create: `eeschema/kisurf_ai_sch_preview_adapter.cpp`
  - Resolves schematic references and adds `SCH_ITEM*` values to `KIGFX::VIEW`.
- Modify: `eeschema/CMakeLists.txt`
  - Add `kisurf_ai_sch_preview_adapter.cpp`.
- Create: `qa/tests/eeschema/test_ai_sch_preview_adapter.cpp`
  - Schematic preview adapter tests.
- Modify: `qa/tests/eeschema/CMakeLists.txt`
  - Add schematic preview test.

## Verification Command Templates

PCB targeted test:

```bat
cmd.exe /d /s /c """C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat"" -arch=x64 -host_arch=x64 >nul && cmake --build out/build/x64-release --target qa_pcbnew -- -j 2 && set KICAD_RUN_FROM_BUILD_DIR=1 && set KICAD_BUILD_PATHS=C:/Users/wenming/source/repos/kisurf/out/build/x64-release/kicad:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/common:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/api:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/common/gal:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/pcbnew:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/eeschema:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/cvpcb && set PATH=D:\Tools\vcpkg\installed\x64-windows\bin;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\kicad;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\common;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\api;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\common\gal;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\pcbnew;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\eeschema;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\cvpcb;%PATH% && out\build\x64-release\qa\tests\pcbnew\qa_pcbnew.exe --run_test=AiPcbPreviewAdapter,AiPcbObjectResolver,AiPcbContextAdapter --log_level=test_suite"
```

Schematic targeted test:

```bat
cmd.exe /d /s /c """C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat"" -arch=x64 -host_arch=x64 >nul && cmake --build out/build/x64-release --target qa_eeschema -- -j 2 && set KICAD_RUN_FROM_BUILD_DIR=1 && set KICAD_BUILD_PATHS=C:/Users/wenming/source/repos/kisurf/out/build/x64-release/kicad:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/common:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/api:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/common/gal:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/pcbnew:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/eeschema:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/cvpcb && set PATH=D:\Tools\vcpkg\installed\x64-windows\bin;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\kicad;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\common;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\api;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\common\gal;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\pcbnew;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\eeschema;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\cvpcb;%PATH% && out\build\x64-release\qa\tests\eeschema\qa_eeschema.exe --run_test=AiSchPreviewAdapter,AiSchObjectResolver,AiSchContextAdapter --log_level=test_suite"
```

Common preview session regression:

```bat
cmd.exe /d /s /c """C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat"" -arch=x64 -host_arch=x64 >nul && cmake --build out/build/x64-release --target qa_common -- -j 2 && set KICAD_RUN_FROM_BUILD_DIR=1 && set KICAD_BUILD_PATHS=C:/Users/wenming/source/repos/kisurf/out/build/x64-release/kicad:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/common:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/api:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/common/gal:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/pcbnew:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/eeschema:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/cvpcb && set PATH=D:\Tools\vcpkg\installed\x64-windows\bin;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\kicad;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\common;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\api;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\common\gal;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\pcbnew;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\eeschema;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\cvpcb;%PATH% && out\build\x64-release\qa\tests\common\qa_common.exe --run_test=AiPreviewSession --log_level=test_suite"
```

Expected final result: exit code `0` and Boost reports no errors. The known
schema warning about `qa/tests/schemas/api.v1.schema.json` is acceptable when
the exit code is `0`.

## Task 1: PCB Preview Adapter

**Files:**
- Create: `pcbnew/kisurf_ai_pcb_preview_adapter.h`
- Create: `pcbnew/kisurf_ai_pcb_preview_adapter.cpp`
- Modify: `pcbnew/CMakeLists.txt`
- Create: `qa/tests/pcbnew/test_ai_pcb_preview_adapter.cpp`
- Modify: `qa/tests/pcbnew/CMakeLists.txt`

- [ ] **Step 1: Write failing PCB preview adapter tests**

Create `qa/tests/pcbnew/test_ai_pcb_preview_adapter.cpp` with tests that:

- Build a board fixture with a footprint and two pads.
- Use `KISURF_AI_PCB_CONTEXT_ADAPTER` to obtain references by label.
- Construct `KIGFX::VIEW`, `KISURF_AI_PCB_OBJECT_RESOLVER`, and the missing
  `KISURF_AI_PCB_PREVIEW_ADAPTER`.
- Use `AI_PREVIEW_SESSION::Show(...)` to verify auto-begin preview behavior.
- Assert `ActivePreviewId() == 1` and `PreviewedItems()` contains the resolved
  `PAD*`.
- Assert unknown references are skipped.
- Assert stale preview ids are ignored.
- Assert `AI_PREVIEW_SESSION::Clear()` resets active id and diagnostics.

- [ ] **Step 2: Register the failing PCB preview test**

Add `test_ai_pcb_preview_adapter.cpp` to `QA_PCBNEW_SRCS` in
`qa/tests/pcbnew/CMakeLists.txt`.

- [ ] **Step 3: Run PCB preview tests to verify RED**

Run the PCB verification command with `--run_test=AiPcbPreviewAdapter`.

Expected RED: compile fails because `kisurf_ai_pcb_preview_adapter.h` does not
exist.

- [ ] **Step 4: Add PCB preview adapter header**

Create `pcbnew/kisurf_ai_pcb_preview_adapter.h`:

```cpp
#pragma once

#include <kisurf/ai/ai_preview_session.h>

#include <cstdint>
#include <vector>

class BOARD_ITEM;
class KISURF_AI_PCB_OBJECT_RESOLVER;

namespace KIGFX
{
class VIEW;
}

class KISURF_AI_PCB_PREVIEW_ADAPTER : public AI_PREVIEW_ADAPTER
{
public:
    KISURF_AI_PCB_PREVIEW_ADAPTER( KISURF_AI_PCB_OBJECT_RESOLVER& aResolver,
                                   KIGFX::VIEW& aView );

    void BeginPreview( uint64_t aPreviewId ) override;
    void ShowObject( uint64_t aPreviewId, const AI_OBJECT_REF& aObject ) override;
    void ClearPreview( uint64_t aPreviewId ) override;

    uint64_t ActivePreviewId() const { return m_ActivePreviewId; }
    const std::vector<BOARD_ITEM*>& PreviewedItems() const { return m_PreviewedItems; }

private:
    KISURF_AI_PCB_OBJECT_RESOLVER& m_Resolver;
    KIGFX::VIEW&                   m_View;
    uint64_t                       m_ActivePreviewId = 0;
    std::vector<BOARD_ITEM*>        m_PreviewedItems;
};
```

- [ ] **Step 5: Add PCB preview adapter implementation**

Create `pcbnew/kisurf_ai_pcb_preview_adapter.cpp`:

```cpp
#include <kisurf_ai_pcb_preview_adapter.h>

#include <kisurf_ai_pcb_object_resolver.h>
#include <view/view.h>

KISURF_AI_PCB_PREVIEW_ADAPTER::KISURF_AI_PCB_PREVIEW_ADAPTER(
        KISURF_AI_PCB_OBJECT_RESOLVER& aResolver, KIGFX::VIEW& aView ) :
        m_Resolver( aResolver ),
        m_View( aView )
{
}

void KISURF_AI_PCB_PREVIEW_ADAPTER::BeginPreview( uint64_t aPreviewId )
{
    m_View.ClearPreview();
    m_ActivePreviewId = aPreviewId;
    m_PreviewedItems.clear();
}

void KISURF_AI_PCB_PREVIEW_ADAPTER::ShowObject( uint64_t aPreviewId,
                                                const AI_OBJECT_REF& aObject )
{
    if( aPreviewId != m_ActivePreviewId )
        return;

    BOARD_ITEM* item = m_Resolver.Resolve( aObject );

    if( !item )
        return;

    m_View.AddToPreview( item, false );
    m_PreviewedItems.push_back( item );
}

void KISURF_AI_PCB_PREVIEW_ADAPTER::ClearPreview( uint64_t aPreviewId )
{
    if( aPreviewId != m_ActivePreviewId )
        return;

    m_View.ClearPreview();
    m_ActivePreviewId = 0;
    m_PreviewedItems.clear();
}
```

Add `kisurf_ai_pcb_preview_adapter.cpp` to `PCBNEW_SRCS` in
`pcbnew/CMakeLists.txt`.

- [ ] **Step 6: Run PCB preview tests to verify GREEN**

Run the PCB verification command with:
`--run_test=AiPcbPreviewAdapter,AiPcbObjectResolver,AiPcbContextAdapter`.

## Task 2: Schematic Preview Adapter

**Files:**
- Create: `eeschema/kisurf_ai_sch_preview_adapter.h`
- Create: `eeschema/kisurf_ai_sch_preview_adapter.cpp`
- Modify: `eeschema/CMakeLists.txt`
- Create: `qa/tests/eeschema/test_ai_sch_preview_adapter.cpp`
- Modify: `qa/tests/eeschema/CMakeLists.txt`

- [ ] **Step 1: Write failing schematic preview adapter tests**

Create `qa/tests/eeschema/test_ai_sch_preview_adapter.cpp` with tests that:

- Build an `SCH_SCREEN` fixture with two `SCH_SYMBOL` objects.
- Use `KISURF_AI_SCH_CONTEXT_ADAPTER` to obtain references by label.
- Construct `KIGFX::VIEW`, `KISURF_AI_SCH_OBJECT_RESOLVER`, and the missing
  `KISURF_AI_SCH_PREVIEW_ADAPTER`.
- Use `AI_PREVIEW_SESSION::Show(...)` to verify auto-begin preview behavior.
- Assert `ActivePreviewId() == 1` and `PreviewedItems()` contains the resolved
  `SCH_SYMBOL*`.
- Assert unknown references are skipped.
- Assert stale preview ids are ignored.
- Assert `AI_PREVIEW_SESSION::Clear()` resets active id and diagnostics.

- [ ] **Step 2: Register the failing schematic preview test**

Add `test_ai_sch_preview_adapter.cpp` to `QA_EESCHEMA_SRCS` in
`qa/tests/eeschema/CMakeLists.txt`.

- [ ] **Step 3: Run schematic preview tests to verify RED**

Run the schematic verification command with `--run_test=AiSchPreviewAdapter`.

Expected RED: compile fails because `kisurf_ai_sch_preview_adapter.h` does not
exist.

- [ ] **Step 4: Add schematic preview adapter header**

Create `eeschema/kisurf_ai_sch_preview_adapter.h`:

```cpp
#pragma once

#include <kisurf/ai/ai_preview_session.h>

#include <cstdint>
#include <vector>

class KISURF_AI_SCH_OBJECT_RESOLVER;
class SCH_ITEM;

namespace KIGFX
{
class VIEW;
}

class KISURF_AI_SCH_PREVIEW_ADAPTER : public AI_PREVIEW_ADAPTER
{
public:
    KISURF_AI_SCH_PREVIEW_ADAPTER( KISURF_AI_SCH_OBJECT_RESOLVER& aResolver,
                                   KIGFX::VIEW& aView );

    void BeginPreview( uint64_t aPreviewId ) override;
    void ShowObject( uint64_t aPreviewId, const AI_OBJECT_REF& aObject ) override;
    void ClearPreview( uint64_t aPreviewId ) override;

    uint64_t ActivePreviewId() const { return m_ActivePreviewId; }
    const std::vector<SCH_ITEM*>& PreviewedItems() const { return m_PreviewedItems; }

private:
    KISURF_AI_SCH_OBJECT_RESOLVER& m_Resolver;
    KIGFX::VIEW&                   m_View;
    uint64_t                       m_ActivePreviewId = 0;
    std::vector<SCH_ITEM*>          m_PreviewedItems;
};
```

- [ ] **Step 5: Add schematic preview adapter implementation**

Create `eeschema/kisurf_ai_sch_preview_adapter.cpp` with the same lifecycle as
the PCB adapter, using `KISURF_AI_SCH_OBJECT_RESOLVER` and `SCH_ITEM*`.

Add `kisurf_ai_sch_preview_adapter.cpp` to the eeschema source list next to the
object resolver.

- [ ] **Step 6: Run schematic preview tests to verify GREEN**

Run the schematic verification command with:
`--run_test=AiSchPreviewAdapter,AiSchObjectResolver,AiSchContextAdapter`.

## Task 3: Common Preview Regression

**Files:** none

- [ ] **Step 1: Run common preview session tests**

Run the common preview session regression command with
`--run_test=AiPreviewSession`.

Expected: exit code `0`.

## Task 4: Final Verification And Commit

- [ ] **Step 1: Run targeted PCB verification**

Run the PCB verification command with:
`--run_test=AiPcbPreviewAdapter,AiPcbObjectResolver,AiPcbContextAdapter`.

- [ ] **Step 2: Run targeted schematic verification**

Run the schematic verification command with:
`--run_test=AiSchPreviewAdapter,AiSchObjectResolver,AiSchContextAdapter`.

- [ ] **Step 3: Run common preview regression**

Run the common verification command with `--run_test=AiPreviewSession`.

- [ ] **Step 4: Run diff checks**

Run:

```powershell
git diff --check
git diff --cached --check
```

Expected: exit code `0`; LF/CRLF warnings are acceptable.

- [ ] **Step 5: Commit**

Run:

```powershell
git add pcbnew/kisurf_ai_pcb_preview_adapter.h pcbnew/kisurf_ai_pcb_preview_adapter.cpp pcbnew/CMakeLists.txt qa/tests/pcbnew/test_ai_pcb_preview_adapter.cpp qa/tests/pcbnew/CMakeLists.txt eeschema/kisurf_ai_sch_preview_adapter.h eeschema/kisurf_ai_sch_preview_adapter.cpp eeschema/CMakeLists.txt qa/tests/eeschema/test_ai_sch_preview_adapter.cpp qa/tests/eeschema/CMakeLists.txt
git commit -m "feat: add ai native preview adapters"
```

## Plan Self-Review

- Spec coverage: all Phase 10 lifecycle, safety, and testing requirements are
  mapped to tasks.
- Placeholder scan: no placeholder or fill-in text remains.
- Type consistency: PCB uses `BOARD_ITEM*`, schematic uses `SCH_ITEM*`, and both
  implement the existing `AI_PREVIEW_ADAPTER` interface.
- Scope check: this plan adds no selection mutation, accepted edit, model
  provider behavior, or UI accept button.
