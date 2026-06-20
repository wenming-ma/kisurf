# AI Provider Check Action Tool Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Declare `kisurf_check_action` alongside `kisurf_run_action` in OpenAI-compatible provider requests.

**Architecture:** Keep the existing action handler as the source of dry-run semantics. Refactor provider tool schema construction into a small helper so both action tools share identical parameters without duplicating a large JSON literal.

**Tech Stack:** C++17, nlohmann JSON, KiCad common AI provider, Boost.Test.

---

## File Structure

- Modify: `docs/superpowers/specs/2026-06-16-kisurf-ai-native-spec-index.md`
- Modify: `common/kisurf/ai/ai_provider.cpp`
- Modify: `qa/tests/common/test_ai_provider.cpp`

## Verification Commands

Provider and action-handler targeted verification:

```bat
cmd.exe /d /s /c """C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat"" -arch=x64 -host_arch=x64 >nul && cmake --build out/build/x64-release --target qa_common -- -j 2 && set KICAD_RUN_FROM_BUILD_DIR=1 && set KICAD_BUILD_PATHS=C:/Users/wenming/source/repos/kisurf/out/build/x64-release/kicad:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/common:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/api:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/common/gal:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/pcbnew:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/eeschema:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/cvpcb && set PATH=D:\Tools\vcpkg\installed\x64-windows\bin;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\kicad;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\common;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\api;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\common\gal;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\pcbnew;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\eeschema;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\cvpcb;%PATH% && out\build\x64-release\qa\tests\common\qa_common.exe --run_test=AiNativeProvider,AiActionToolCallHandler --log_level=test_suite
```

The known missing schema warning is acceptable only when Boost reports no errors.

## Task 1: Provider Tool Declaration Test

**Files:**
- Modify: `qa/tests/common/test_ai_provider.cpp`

- [ ] **Step 1: Update the existing tool declaration test**

In `OpenAiProviderDeclaresSingleKiSurfTool`, rename the test to:

```cpp
BOOST_AUTO_TEST_CASE( OpenAiProviderDeclaresKiSurfActionTools )
```

Replace the checks with:

```cpp
BOOST_REQUIRE( body.contains( "tools" ) );
BOOST_REQUIRE( body["tools"].is_array() );
BOOST_REQUIRE_EQUAL( body["tools"].size(), 2 );

std::vector<std::string> toolNames;

for( const nlohmann::json& tool : body["tools"] )
{
    BOOST_CHECK_EQUAL( tool["type"].get<std::string>(), "function" );
    toolNames.push_back( tool["function"]["name"].get<std::string>() );
}

BOOST_CHECK( std::find( toolNames.begin(), toolNames.end(), "kisurf_run_action" )
             != toolNames.end() );
BOOST_CHECK( std::find( toolNames.begin(), toolNames.end(), "kisurf_check_action" )
             != toolNames.end() );
BOOST_REQUIRE( body.contains( "parallel_tool_calls" ) );
BOOST_CHECK( !body["parallel_tool_calls"].get<bool>() );
```

Add `#include <algorithm>` and `#include <vector>` if the file does not already
include them.

- [ ] **Step 2: Run tests to verify RED**

Run the provider/action-handler targeted verification command.

Expected: provider tool declaration test fails because the tools array still has
one entry.

## Task 2: Provider Tool Schema Helper

**Files:**
- Modify: `common/kisurf/ai/ai_provider.cpp`

- [ ] **Step 1: Add tool schema helpers**

Add to the anonymous namespace:

```cpp
nlohmann::json actionToolParameters()
{
    return {
        { "type", "object" },
        { "properties",
          { { "action",
              { { "type", "string" },
                { "description",
                  "Native KiCad/KiSurf action name from the current action catalog." } } },
            { "arguments",
              { { "type", "object" },
                { "description", "Optional action-specific arguments." },
                { "additionalProperties", true } } },
            { "dry_run",
              { { "type", "boolean" },
                { "description",
                  "When true, check policy and preview feasibility without executing." } } } } },
        { "required", nlohmann::json::array( { "action" } ) },
        { "additionalProperties", false } };
}

nlohmann::json functionTool( const char* aName, const char* aDescription )
{
    return { { "type", "function" },
             { "function",
               { { "name", aName },
                 { "description", aDescription },
                 { "parameters", actionToolParameters() } } } };
}
```

- [ ] **Step 2: Replace the inline tools literal**

Replace the existing one-entry `body["tools"]` assignment with:

```cpp
body["tools"] = nlohmann::json::array(
        { functionTool( "kisurf_run_action",
                        "Request a KiSurf editor action by native action name. Local KiSurf policy decides whether the action can run." ),
          functionTool( "kisurf_check_action",
                        "Check whether a KiSurf editor action is known, available, and allowed without executing it." ) } );
```

- [ ] **Step 3: Run provider/action-handler verification**

Run the verification command.

Expected: provider and action-handler suites pass.

## Task 3: Spec Index, Diff Check, And Commit

**Files:**
- All files above.

- [ ] **Step 1: Update spec index**

Add:

```markdown
19. [AI Provider Check Action Tool](./2026-06-16-ai-provider-check-action-tool-design.md)
    - Defines OpenAI-compatible declaration of the dry-run `kisurf_check_action` tool.
```

Add implementation order:

```markdown
23. Phase 16 provider check-action declaration that lets models dry-run action policy before requesting execution.
```

- [ ] **Step 2: Run diff hygiene checks**

Run:

```powershell
git diff --check
git status --short
git diff --stat
```

Expected: no whitespace errors and only files from this plan changed.

- [ ] **Step 3: Commit**

```powershell
git add docs\superpowers\specs\2026-06-16-ai-provider-check-action-tool-design.md docs\superpowers\specs\2026-06-16-kisurf-ai-native-spec-index.md docs\superpowers\plans\2026-06-16-ai-provider-check-action-tool-implementation.md common\kisurf\ai\ai_provider.cpp qa\tests\common\test_ai_provider.cpp
git commit -m "feat: declare ai check action tool"
```

## Plan Self-Review

- Spec coverage: tasks cover provider schema declaration, shared parameters,
  dry-run safety, tests, and verification.
- Open-marker scan: no unresolved placeholders remain.
- Type consistency: test names and helper names match the planned provider code.
