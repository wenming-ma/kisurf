# AI Structured Context JSON Implementation Plan

Date: 2026-06-16

## Checklist

- [x] Add the structured-context spec and link it from the AI-native spec index.
- [x] Add RED tests for context JSON serialization and provider transport.
- [x] Implement bounded `AI_CONTEXT_SNAPSHOT::AsJsonText`.
- [x] Append structured context JSON in the OpenAI-compatible provider user message.
- [x] Run targeted provider/type tests.
- [x] Run diff hygiene and commit the slice.

## Target Verification

```powershell
cmd.exe /d /s /c """C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat"" -arch=x64 -host_arch=x64 >nul && cmake --build out/build/x64-release --target qa_common -- -j 2 && set KICAD_RUN_FROM_BUILD_DIR=1 && set KICAD_BUILD_PATHS=C:/Users/wenming/source/repos/kisurf/out/build/x64-release/kicad:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/common:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/api:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/common/gal && set PATH=D:\Tools\vcpkg\installed\x64-windows\bin;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\kicad;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\common;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\api;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\common\gal;%PATH% && out\build\x64-release\qa\tests\common\qa_common.exe --run_test=AiNativeTypes,AiNativeProvider --log_level=test_suite
```
