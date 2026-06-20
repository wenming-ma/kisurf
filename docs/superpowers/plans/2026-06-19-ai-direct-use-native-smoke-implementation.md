# AI Direct-Use Native Smoke Implementation Plan

## Checklist

1. Add the design spec and this plan.
2. Add `test_ai_direct_use_smoke.cpp` with:
   - environment alias smoke;
   - provider tool-surface smoke;
   - workspace-view semantic smoke;
   - operation-only preview capability smoke.
3. Register the new test source in `qa/tests/common/CMakeLists.txt`.
4. Update the README verification section so developers have a single command for the direct-use smoke.
5. Verify with:
   - `cmake --build out/build/x64-release --target qa_common`;
   - `out/build/x64-release/qa/tests/common/qa_common.exe --run_test=AiDirectUseSmoke`;
   - focused AI suite run;
   - `cmake --build out/build/x64-release --target pcbnew`;
   - `cmake --build out/build/x64-release --target eeschema`;
   - `git diff --check`;
   - dynamic secret scan.

## Notes

This slice should not require production code. If the smoke fails, fix the production path that violates the direct-use contract rather than weakening the test.
