# AI PCB Arc Context Coverage Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add model-visible PCB arc refs and resolver support for `PCB_ARC_T`.

**Architecture:** Extend the existing PCB context adapter routing-object path to include arc tracks from `BOARD::Tracks()`. Reuse UUID/type identity and deterministic labels; extend the resolver's routing type check instead of adding a parallel lookup path.

**Tech Stack:** KiCad C++, Boost.Test, existing `AI_CONTEXT_INDEX`, `AI_OBJECT_REF`, and PCB test binaries.

---

## Checklist

- [x] Add the PCB arc context coverage spec and link it from the AI-native spec index.
- [x] Add RED coverage for visible and selected arc refs.
- [x] Add RED coverage for resolving arc refs.
- [x] Implement arc refs in `KISURF_AI_PCB_CONTEXT_ADAPTER`.
- [x] Implement arc resolution in `KISURF_AI_PCB_OBJECT_RESOLVER`.
- [x] Run targeted PCB AI context, resolver, preview, and move edit tests.
- [x] Run diff hygiene and commit the slice.

## Target Verification

```powershell
cmd.exe /d /s /c """C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat"" -arch=x64 -host_arch=x64 >nul && cmake --build out/build/x64-release --target qa_pcbnew qa_common -- -j 2 && set KICAD_RUN_FROM_BUILD_DIR=1 && set KICAD_BUILD_PATHS=C:/Users/wenming/source/repos/kisurf/out/build/x64-release/kicad:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/common:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/api:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/common/gal:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/pcbnew && set PATH=D:\Tools\vcpkg\installed\x64-windows\bin;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\kicad;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\common;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\api;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\common\gal;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\pcbnew;%PATH% && out\build\x64-release\qa\tests\pcbnew\qa_pcbnew.exe --run_test=AiPcbContextAdapter,AiPcbObjectResolver,AiPcbPreviewAdapter,AiPcbMoveEditAdapter --log_level=test_suite && out\build\x64-release\qa\tests\common\qa_common.exe --run_test=AiContextIndex,AiNativeTypes,AiAgentSuggestionProvider --log_level=test_suite
```
