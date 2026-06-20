# AI Schematic Structured Object Details Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add structured schematic object details to AI context refs for common schematic items.

**Architecture:** Reuse `AI_OBJECT_REF.m_DetailsJson` as a compact producer-owned JSON object. Keep resolver identity unchanged and keep this slice inside the schematic context adapter plus tests.

**Tech Stack:** KiCad C++, Boost.Test, existing eeschema AI adapter tests.

---

## Checklist

- [x] Add the schematic structured object details spec and link it from the AI-native spec index.
- [x] Add RED eeschema context coverage for symbol, wire, bus, label, junction, and no-connect details.
- [x] Implement schematic details JSON and stable labels in `KISURF_AI_SCH_CONTEXT_ADAPTER`.
- [x] Run targeted eeschema AI adapter tests.
- [x] Run diff hygiene and commit the slice.

## Target Verification

```powershell
cmd.exe /d /s /c """C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat"" -arch=x64 -host_arch=x64 >nul && cmake --build out/build/x64-release --target qa_eeschema -- -j 2 && set KICAD_RUN_FROM_BUILD_DIR=1 && set KICAD_BUILD_PATHS=C:/Users/wenming/source/repos/kisurf/out/build/x64-release/kicad:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/common:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/api:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/common/gal:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/eeschema && set PATH=D:\Tools\vcpkg\installed\x64-windows\bin;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\kicad;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\common;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\api;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\common\gal;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\eeschema;%PATH% && out\build\x64-release\qa\tests\eeschema\qa_eeschema.exe --run_test=AiSchContextAdapter,AiSchObjectResolver,AiSchPreviewAdapter,AiSchMoveEditAdapter --log_level=test_suite
```
