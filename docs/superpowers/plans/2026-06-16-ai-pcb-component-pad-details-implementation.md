# AI PCB Component And Pad Details Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add structured PCB footprint and pad details to AI context refs.

**Architecture:** Reuse the existing `AI_OBJECT_REF.m_DetailsJson` channel. Keep changes inside `KISURF_AI_PCB_CONTEXT_ADAPTER` plus focused PCB adapter tests; resolver, preview, edit, and provider code should not change.

**Tech Stack:** KiCad C++, Boost.Test, existing PCB AI adapter tests.

---

## Checklist

- [x] Add the PCB component/pad details spec and link it from the AI-native spec index.
- [x] Add RED PCB context coverage for footprint and pad structured details.
- [x] Implement footprint and pad details JSON in `KISURF_AI_PCB_CONTEXT_ADAPTER`.
- [x] Run targeted PCB AI adapter tests.
- [x] Run diff hygiene and commit the slice.

## Target Verification

```powershell
cmd.exe /d /c '"C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 >nul && cmake --build out/build/x64-release --target qa_pcbnew -- -j 2 && set KICAD_RUN_FROM_BUILD_DIR=1&& set KICAD_BUILD_PATHS=C:/Users/wenming/source/repos/kisurf/out/build/x64-release/kicad:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/common:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/api:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/common/gal:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/pcbnew&& set PATH=D:\Tools\vcpkg\installed\x64-windows\bin;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\kicad;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\common;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\api;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\common\gal;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\pcbnew;%PATH%&& out\build\x64-release\qa\tests\pcbnew\qa_pcbnew.exe --run_test=AiPcbContextAdapter,AiPcbObjectResolver,AiPcbPreviewAdapter,AiPcbMoveEditAdapter --log_level=test_suite'
```
