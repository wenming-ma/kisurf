# AI Runtime Missing Handler Result Implementation Plan

Date: 2026-06-16

## Checklist

- [x] Add the missing-handler result spec and link it from the AI-native spec index.
- [x] Add RED runtime tests for no-handler `ToolResult` activity and result JSON.
- [x] Implement no-handler error/result population in `AI_RUNTIME::Submit`.
- [x] Run targeted runtime tests.
- [x] Run diff hygiene and commit the slice.

## Target Verification

```powershell
cmd.exe /d /s /c """C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat"" -arch=x64 -host_arch=x64 >nul && cmake --build out/build/x64-release --target qa_common -- -j 2 && set KICAD_RUN_FROM_BUILD_DIR=1 && set KICAD_BUILD_PATHS=C:/Users/wenming/source/repos/kisurf/out/build/x64-release/kicad:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/common:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/api:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/common/gal && set PATH=D:\Tools\vcpkg\installed\x64-windows\bin;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\kicad;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\common;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\api;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\common\gal;%PATH% && out\build\x64-release\qa\tests\common\qa_common.exe --run_test=AiNativeRuntime --log_level=test_suite
```
