# AI PCB Drawing Shape Context Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Expose board-level `PCB_SHAPE_T` drawing objects to AI context and object resolution.

**Architecture:** Extend the PCB context adapter to emit shape refs from `BOARD::Drawings()` and extend the PCB object resolver to resolve those refs by UUID plus `KICAD_T`. Keep preview and edit adapters unchanged because they already operate on resolved `BOARD_ITEM` clones/moves.

**Tech Stack:** KiCad C++, Boost.Test, existing PCB AI adapter/resolver tests.

---

## Checklist

- [x] Add the PCB drawing shape context spec and link it from the AI-native spec index.
- [x] Add RED PCB context coverage for selected Edge.Cuts segment refs and details.
- [x] Add RED PCB resolver coverage for `PCB_SHAPE_T` refs.
- [x] Implement shape refs/details in `KISURF_AI_PCB_CONTEXT_ADAPTER`.
- [x] Implement `PCB_SHAPE_T` resolution in `KISURF_AI_PCB_OBJECT_RESOLVER`.
- [x] Run targeted PCB AI adapter tests.
- [x] Run diff hygiene and commit the slice.

## Target Verification

```powershell
cmd.exe /d /c '"C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 >nul && cmake --build out/build/x64-release --target qa_pcbnew -- -j 2 && set KICAD_RUN_FROM_BUILD_DIR=1&& set KICAD_BUILD_PATHS=C:/Users/wenming/source/repos/kisurf/out/build/x64-release/kicad:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/common:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/api:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/common/gal:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/pcbnew&& set PATH=D:\Tools\vcpkg\installed\x64-windows\bin;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\kicad;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\common;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\api;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\common\gal;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\pcbnew;%PATH%&& out\build\x64-release\qa\tests\pcbnew\qa_pcbnew.exe --run_test=AiPcbContextAdapter,AiPcbObjectResolver,AiPcbPreviewAdapter,AiPcbMoveEditAdapter --log_level=test_suite'
```
