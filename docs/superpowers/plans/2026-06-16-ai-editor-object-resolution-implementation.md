# AI Editor Object Resolution Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add read-only PCB and schematic object resolvers that map `AI_OBJECT_REF`
records back to native editor objects.

**Architecture:** Keep common AI types serializable and editor-agnostic. Add
editor-local resolver classes beside the existing PCB/SCH context adapters. The
resolvers only inspect current model objects and return native pointers when
UUID and type both match.

**Tech Stack:** C++20, Boost.Test, KiCad PCB/SCH editor modules, `qa_pcbnew`,
`qa_eeschema`.

---

## File Structure

- Create: `pcbnew/kisurf_ai_pcb_object_resolver.h`
  - PCB read-only resolver API.
- Create: `pcbnew/kisurf_ai_pcb_object_resolver.cpp`
  - Pad lookup implementation over `BOARD::Footprints()` and `FOOTPRINT::Pads()`.
- Modify: `pcbnew/CMakeLists.txt`
  - Add `kisurf_ai_pcb_object_resolver.cpp` beside the context adapter.
- Create: `qa/tests/pcbnew/test_ai_pcb_object_resolver.cpp`
  - PCB resolver tests.
- Modify: `qa/tests/pcbnew/CMakeLists.txt`
  - Add the PCB resolver test.
- Create: `eeschema/kisurf_ai_sch_object_resolver.h`
  - Schematic read-only resolver API.
- Create: `eeschema/kisurf_ai_sch_object_resolver.cpp`
  - Screen item lookup implementation over `SCH_SCREEN::Items()`.
- Modify: `eeschema/CMakeLists.txt`
  - Add `kisurf_ai_sch_object_resolver.cpp` beside the context adapter.
- Create: `qa/tests/eeschema/test_ai_sch_object_resolver.cpp`
  - Schematic resolver tests.
- Modify: `qa/tests/eeschema/CMakeLists.txt`
  - Add the schematic resolver test.

## Verification Command Templates

PCB targeted test:

```bat
cmd.exe /d /s /c """C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat"" -arch=x64 -host_arch=x64 >nul && cmake --build out/build/x64-release --target qa_pcbnew -- -j 2 && set KICAD_RUN_FROM_BUILD_DIR=1 && set KICAD_BUILD_PATHS=C:/Users/wenming/source/repos/kisurf/out/build/x64-release/kicad:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/common:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/api:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/common/gal:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/pcbnew:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/eeschema:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/cvpcb && set PATH=D:\Tools\vcpkg\installed\x64-windows\bin;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\kicad;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\common;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\api;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\common\gal;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\pcbnew;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\eeschema;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\cvpcb;%PATH% && out\build\x64-release\qa\tests\pcbnew\qa_pcbnew.exe --run_test=AiPcbObjectResolver,AiPcbContextAdapter --log_level=test_suite"
```

Schematic targeted test:

```bat
cmd.exe /d /s /c """C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat"" -arch=x64 -host_arch=x64 >nul && cmake --build out/build/x64-release --target qa_eeschema -- -j 2 && set KICAD_RUN_FROM_BUILD_DIR=1 && set KICAD_BUILD_PATHS=C:/Users/wenming/source/repos/kisurf/out/build/x64-release/kicad:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/common:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/api:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/common/gal:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/pcbnew:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/eeschema:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/cvpcb && set PATH=D:\Tools\vcpkg\installed\x64-windows\bin;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\kicad;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\common;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\api;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\common\gal;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\pcbnew;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\eeschema;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\cvpcb;%PATH% && out\build\x64-release\qa\tests\eeschema\qa_eeschema.exe --run_test=AiSchObjectResolver,AiSchContextAdapter --log_level=test_suite"
```

Expected final result: exit code `0` and Boost reports no errors. The known
schema warning about `qa/tests/schemas/api.v1.schema.json` is acceptable when
the exit code is `0`.

## Task 1: PCB Object Resolver

**Files:**
- Create: `pcbnew/kisurf_ai_pcb_object_resolver.h`
- Create: `pcbnew/kisurf_ai_pcb_object_resolver.cpp`
- Modify: `pcbnew/CMakeLists.txt`
- Create: `qa/tests/pcbnew/test_ai_pcb_object_resolver.cpp`
- Modify: `qa/tests/pcbnew/CMakeLists.txt`

- [ ] **Step 1: Write failing PCB resolver tests**

Create `qa/tests/pcbnew/test_ai_pcb_object_resolver.cpp` with tests that:

- Build a `BOARD`, `FOOTPRINT`, and two `PAD` objects.
- Use `KISURF_AI_PCB_CONTEXT_ADAPTER` to obtain references.
- Assert `KISURF_AI_PCB_OBJECT_RESOLVER::Resolve(...)` returns the original pad
  for a context-emitted reference.
- Assert unknown UUID returns `nullptr`.
- Assert a matching UUID with a non-pad type, such as `PCB_TRACE_T`, returns
  `nullptr`.
- Assert `ResolveAll(...)` skips unresolved references and preserves order.

- [ ] **Step 2: Register the failing PCB resolver test**

Add `test_ai_pcb_object_resolver.cpp` to `QA_PCBNEW_SRCS` in
`qa/tests/pcbnew/CMakeLists.txt`.

- [ ] **Step 3: Run PCB tests to verify RED**

Run the PCB verification command with `--run_test=AiPcbObjectResolver`.

Expected RED: compile fails because `kisurf_ai_pcb_object_resolver.h` does not
exist.

- [ ] **Step 4: Add PCB resolver header**

Create `pcbnew/kisurf_ai_pcb_object_resolver.h`:

```cpp
#pragma once

#include <kisurf/ai/ai_types.h>

#include <vector>

class BOARD;
class BOARD_ITEM;

class KISURF_AI_PCB_OBJECT_RESOLVER
{
public:
    explicit KISURF_AI_PCB_OBJECT_RESOLVER( BOARD& aBoard );

    BOARD_ITEM* Resolve( const AI_OBJECT_REF& aRef ) const;
    std::vector<BOARD_ITEM*> ResolveAll( const std::vector<AI_OBJECT_REF>& aRefs ) const;

private:
    BOARD& m_Board;
};
```

- [ ] **Step 5: Add PCB resolver implementation**

Create `pcbnew/kisurf_ai_pcb_object_resolver.cpp`:

```cpp
#include <kisurf_ai_pcb_object_resolver.h>

#include <board.h>
#include <footprint.h>
#include <pad.h>

KISURF_AI_PCB_OBJECT_RESOLVER::KISURF_AI_PCB_OBJECT_RESOLVER( BOARD& aBoard ) :
        m_Board( aBoard )
{
}

BOARD_ITEM* KISURF_AI_PCB_OBJECT_RESOLVER::Resolve( const AI_OBJECT_REF& aRef ) const
{
    if( !aRef.IsValid() || aRef.m_Type != PCB_PAD_T )
        return nullptr;

    for( FOOTPRINT* footprint : m_Board.Footprints() )
    {
        for( PAD* pad : footprint->Pads() )
        {
            if( pad->m_Uuid == aRef.m_Uuid && pad->Type() == aRef.m_Type )
                return pad;
        }
    }

    return nullptr;
}

std::vector<BOARD_ITEM*> KISURF_AI_PCB_OBJECT_RESOLVER::ResolveAll(
        const std::vector<AI_OBJECT_REF>& aRefs ) const
{
    std::vector<BOARD_ITEM*> resolved;

    for( const AI_OBJECT_REF& ref : aRefs )
    {
        if( BOARD_ITEM* item = Resolve( ref ) )
            resolved.push_back( item );
    }

    return resolved;
}
```

Add `kisurf_ai_pcb_object_resolver.cpp` to `PCBNEW_SRCS` in
`pcbnew/CMakeLists.txt`, next to `kisurf_ai_pcb_context_adapter.cpp`.

- [ ] **Step 6: Run PCB tests to verify GREEN**

Run the PCB verification command with `--run_test=AiPcbObjectResolver,AiPcbContextAdapter`.

## Task 2: Schematic Object Resolver

**Files:**
- Create: `eeschema/kisurf_ai_sch_object_resolver.h`
- Create: `eeschema/kisurf_ai_sch_object_resolver.cpp`
- Modify: `eeschema/CMakeLists.txt`
- Create: `qa/tests/eeschema/test_ai_sch_object_resolver.cpp`
- Modify: `qa/tests/eeschema/CMakeLists.txt`

- [ ] **Step 1: Write failing schematic resolver tests**

Create `qa/tests/eeschema/test_ai_sch_object_resolver.cpp` with tests that:

- Build an `SCH_SCREEN` with two `SCH_SYMBOL` objects.
- Use `KISURF_AI_SCH_CONTEXT_ADAPTER` to obtain references.
- Assert `KISURF_AI_SCH_OBJECT_RESOLVER::Resolve(...)` returns the original
  symbol for a context-emitted reference.
- Assert unknown UUID returns `nullptr`.
- Assert a matching UUID with a non-symbol type, such as `SCH_LINE_T`, returns
  `nullptr`.
- Assert `ResolveAll(...)` skips unresolved references and preserves order.

- [ ] **Step 2: Register the failing schematic resolver test**

Add `test_ai_sch_object_resolver.cpp` to `QA_EESCHEMA_SRCS` in
`qa/tests/eeschema/CMakeLists.txt`.

- [ ] **Step 3: Run schematic tests to verify RED**

Run the schematic verification command with `--run_test=AiSchObjectResolver`.

Expected RED: compile fails because `kisurf_ai_sch_object_resolver.h` does not
exist.

- [ ] **Step 4: Add schematic resolver header**

Create `eeschema/kisurf_ai_sch_object_resolver.h`:

```cpp
#pragma once

#include <kisurf/ai/ai_types.h>

#include <vector>

class SCH_ITEM;
class SCH_SCREEN;

class KISURF_AI_SCH_OBJECT_RESOLVER
{
public:
    explicit KISURF_AI_SCH_OBJECT_RESOLVER( SCH_SCREEN& aScreen );

    SCH_ITEM* Resolve( const AI_OBJECT_REF& aRef ) const;
    std::vector<SCH_ITEM*> ResolveAll( const std::vector<AI_OBJECT_REF>& aRefs ) const;

private:
    SCH_SCREEN& m_Screen;
};
```

- [ ] **Step 5: Add schematic resolver implementation**

Create `eeschema/kisurf_ai_sch_object_resolver.cpp`:

```cpp
#include <kisurf_ai_sch_object_resolver.h>

#include <sch_item.h>
#include <sch_screen.h>

KISURF_AI_SCH_OBJECT_RESOLVER::KISURF_AI_SCH_OBJECT_RESOLVER( SCH_SCREEN& aScreen ) :
        m_Screen( aScreen )
{
}

SCH_ITEM* KISURF_AI_SCH_OBJECT_RESOLVER::Resolve( const AI_OBJECT_REF& aRef ) const
{
    if( !aRef.IsValid() )
        return nullptr;

    for( SCH_ITEM* item : m_Screen.Items() )
    {
        if( item->m_Uuid == aRef.m_Uuid && item->Type() == aRef.m_Type )
            return item;
    }

    return nullptr;
}

std::vector<SCH_ITEM*> KISURF_AI_SCH_OBJECT_RESOLVER::ResolveAll(
        const std::vector<AI_OBJECT_REF>& aRefs ) const
{
    std::vector<SCH_ITEM*> resolved;

    for( const AI_OBJECT_REF& ref : aRefs )
    {
        if( SCH_ITEM* item = Resolve( ref ) )
            resolved.push_back( item );
    }

    return resolved;
}
```

Add `kisurf_ai_sch_object_resolver.cpp` to the eeschema source list next to
`kisurf_ai_sch_context_adapter.cpp`.

- [ ] **Step 6: Run schematic tests to verify GREEN**

Run the schematic verification command with
`--run_test=AiSchObjectResolver,AiSchContextAdapter`.

## Task 3: Final Verification And Commit

- [ ] **Step 1: Run targeted PCB verification**

Run:

```bat
out\build\x64-release\qa\tests\pcbnew\qa_pcbnew.exe --run_test=AiPcbObjectResolver,AiPcbContextAdapter --log_level=test_suite
```

inside the command template above.

- [ ] **Step 2: Run targeted schematic verification**

Run:

```bat
out\build\x64-release\qa\tests\eeschema\qa_eeschema.exe --run_test=AiSchObjectResolver,AiSchContextAdapter --log_level=test_suite
```

inside the command template above.

- [ ] **Step 3: Run diff checks**

Run:

```powershell
git diff --check
git diff --cached --check
```

Expected: exit code `0`; LF/CRLF warnings are acceptable.

- [ ] **Step 4: Commit**

Run:

```powershell
git add pcbnew/kisurf_ai_pcb_object_resolver.h pcbnew/kisurf_ai_pcb_object_resolver.cpp pcbnew/CMakeLists.txt qa/tests/pcbnew/test_ai_pcb_object_resolver.cpp qa/tests/pcbnew/CMakeLists.txt eeschema/kisurf_ai_sch_object_resolver.h eeschema/kisurf_ai_sch_object_resolver.cpp eeschema/CMakeLists.txt qa/tests/eeschema/test_ai_sch_object_resolver.cpp qa/tests/eeschema/CMakeLists.txt
git commit -m "feat: add ai editor object resolvers"
```

## Plan Self-Review

- Spec coverage: all goals and non-goals from the object resolution spec are
  mapped to files and tests.
- Placeholder scan: no placeholder or fill-in text remains.
- Type consistency: PCB returns `BOARD_ITEM*`, schematic returns `SCH_ITEM*`,
  and both consume existing `AI_OBJECT_REF` values.
- Scope check: the plan adds no preview drawing, no commit path, and no IPC
  API.
