# AI Suggestion Operation Parser Implementation Plan

Date: 2026-06-16

## Checklist

- [x] Add the operation-parser spec and link it from the AI-native spec index.
- [x] Add RED common tests for valid and invalid suggestion operation payloads.
- [x] Add `ai_suggestion_operations` common header/source and CMake entries.
- [x] Replace duplicated PCB/SCH frame move-argument parsers with the common parser.
- [x] Run targeted common, PCB, and schematic AI tests.
- [x] Run diff hygiene and commit the slice.

## Target Verification

```powershell
cmd.exe /d /s /c """C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat"" -arch=x64 -host_arch=x64 >nul && cmake --build out/build/x64-release --target qa_common qa_pcbnew qa_eeschema -- -j 2 && set KICAD_RUN_FROM_BUILD_DIR=1 && set KICAD_BUILD_PATHS=C:/Users/wenming/source/repos/kisurf/out/build/x64-release/kicad:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/common:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/api:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/common/gal:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/pcbnew:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/eeschema:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/cvpcb && set PATH=D:\Tools\vcpkg\installed\x64-windows\bin;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\kicad;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\common;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\api;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\common\gal;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\pcbnew;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\eeschema;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\cvpcb;%PATH% && out\build\x64-release\qa\tests\common\qa_common.exe --run_test=AiSuggestionOperations,AiAgentSuggestionProvider,AiAgentPanelModel --log_level=test_suite && out\build\x64-release\qa\tests\pcbnew\qa_pcbnew.exe --run_test=AiPcbPreviewAdapter,AiPcbMoveEditAdapter --log_level=test_suite && out\build\x64-release\qa\tests\eeschema\qa_eeschema.exe --run_test=AiSchPreviewAdapter,AiSchMoveEditAdapter --log_level=test_suite
```
