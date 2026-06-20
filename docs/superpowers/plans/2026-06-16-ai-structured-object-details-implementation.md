# AI Structured Object Details Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add optional structured details to AI object refs and populate PCB routing refs.

**Architecture:** Keep object identity unchanged as UUID plus KICAD_T. Store producer-owned compact JSON details on `AI_OBJECT_REF`, render them in prompt text, and parse them into nested `details` fields for structured JSON context.

**Tech Stack:** KiCad C++, nlohmann JSON, Boost.Test, existing AI context and PCB adapter tests.

---

## Checklist

- [x] Add the structured object details spec and link it from the AI-native spec index.
- [x] Add RED common coverage for object ref details in prompt text and structured JSON.
- [x] Add RED PCB coverage for track, arc, and via details.
- [x] Implement optional details storage on `AI_OBJECT_REF`.
- [x] Serialize details in prompt text and structured context JSON.
- [x] Populate PCB routing details in `KISURF_AI_PCB_CONTEXT_ADAPTER`.
- [x] Fix padstack enum-map initialization order exposed by via property startup.
- [x] Run targeted common and PCB AI context tests.
- [ ] Run diff hygiene and commit the slice.

## Target Verification

```powershell
cmd.exe /d /s /c """C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat"" -arch=x64 -host_arch=x64 >nul && cmake --build out/build/x64-release --target qa_common qa_pcbnew -- -j 2 && set KICAD_RUN_FROM_BUILD_DIR=1 && set KICAD_BUILD_PATHS=C:/Users/wenming/source/repos/kisurf/out/build/x64-release/kicad:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/common:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/api:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/common/gal:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/pcbnew && set PATH=D:\Tools\vcpkg\installed\x64-windows\bin;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\kicad;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\common;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\api;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\common\gal;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\pcbnew;%PATH% && out\build\x64-release\qa\tests\common\qa_common.exe --run_test=AiNativeTypes,AiContextIndex,AiAgentSuggestionProvider --log_level=test_suite && out\build\x64-release\qa\tests\pcbnew\qa_pcbnew.exe --run_test=AiPcbContextAdapter,AiPcbObjectResolver,AiPcbPreviewAdapter,AiPcbMoveEditAdapter --log_level=test_suite
```
