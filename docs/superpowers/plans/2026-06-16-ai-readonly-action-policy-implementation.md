# AI Read-Only Action Policy Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Let enabled read-only catalog actions run without explicit per-action allowlist entries while keeping interactive, modifying, and destructive gates intact.

**Architecture:** Update `AI_TOOL_EXECUTION_POLICY::Evaluate(...)` so safety classification is considered before the explicit allowlist. Read-only actions become intrinsically allowed; interactive actions still require `AllowAction(...)`; modifying and destructive actions remain denied even if allowlisted.

**Tech Stack:** C++17, KiSurf common AI tool execution, Boost.Test.

---

## File Structure

- Modify: `common/kisurf/ai/ai_tool_execution.cpp`
  - Reorder policy evaluation and auto-allow read-only descriptors.
- Modify: `qa/tests/common/test_ai_tool_execution.cpp`
  - Add RED tests and update expectations for read-only policy behavior.
- Modify: `docs/superpowers/specs/2026-06-16-kisurf-ai-native-spec-index.md`
  - Register this spec and implementation phase.

## Verification Commands

Tool policy and handler verification:

```bat
cmd.exe /d /s /c """C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat"" -arch=x64 -host_arch=x64 >nul && cmake --build out/build/x64-release --target qa_common -- -j 2 && set KICAD_RUN_FROM_BUILD_DIR=1 && set KICAD_BUILD_PATHS=C:/Users/wenming/source/repos/kisurf/out/build/x64-release/kicad:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/common:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/api:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/common/gal:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/pcbnew:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/eeschema:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/cvpcb && set PATH=D:\Tools\vcpkg\installed\x64-windows\bin;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\kicad;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\common;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\api;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\common\gal;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\pcbnew;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\eeschema;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\cvpcb;%PATH% && out\build\x64-release\qa\tests\common\qa_common.exe --run_test=AiToolExecution,AiActionToolCallHandler,AiNativeRuntime --log_level=test_suite
```

The known missing schema warning is acceptable only when Boost reports no errors.

## Task 1: RED Policy Tests

**Files:**
- Modify: `qa/tests/common/test_ai_tool_execution.cpp`

- [x] **Step 1: Replace the read-only allowlist test**

Rename `PolicyAllowsAllowlistedReadonlyAction` to:

```cpp
BOOST_AUTO_TEST_CASE( PolicyAllowsReadonlyActionWithoutAllowlist )
```

Remove the `policy.AllowAction(...)` call from that test. Keep the same
read-only descriptor and expected allowed result.

- [x] **Step 2: Update unsafe policy test**

In `PolicyDeniesUnsafeActions`, replace the current `notAllowlisted` read-only
descriptor with an interactive descriptor:

```cpp
AI_TOOL_INVOCATION_REQUEST notAllowlisted;
notAllowlisted.m_Action =
        actionDescriptor( wxS( "pcbnew.Interactive.dragPan" ),
                          AI_ACTION_SAFETY::Interactive );
BOOST_CHECK_EQUAL( policy.Evaluate( notAllowlisted ).m_ErrorCode,
                   wxString( wxS( "not_allowlisted" ) ) );
```

- [x] **Step 3: Update executor run test**

In `ExecutorAuditsAndRunsOnlyAllowedCalls`, remove the `policy.AllowAction(...)`
call. The test should still expect the read-only action to execute.

- [x] **Step 4: Update denied/dry-run test**

In `ExecutorDoesNotRunDeniedOrDryRunCalls`, make the denied request interactive:

```cpp
denied.m_Action =
        actionDescriptor( wxS( "common.Interactive.someTool" ),
                          AI_ACTION_SAFETY::Interactive );
```

Keep the later `policy.AllowAction( wxS( "common.Control.showAgentPanel" ) )`
call for the dry-run read-only action, even though it is no longer required, so
the test still focuses on dry-run runner suppression.

- [x] **Step 5: Run RED verification**

Run the tool policy and handler verification command.

Expected: the renamed policy test and executor run test fail because read-only
actions still require explicit allowlist entries.

## Task 2: Policy Implementation

**Files:**
- Modify: `common/kisurf/ai/ai_tool_execution.cpp`

- [x] **Step 1: Reorder policy checks**

Update `AI_TOOL_EXECUTION_POLICY::Evaluate(...)` to:

```cpp
if( aRequest.m_Action.m_Safety == AI_ACTION_SAFETY::Destructive )
{
    deny( result, wxS( "destructive_denied" ),
          wxS( "Destructive actions cannot be executed by model output." ) );
    return result;
}

if( aRequest.m_Action.m_Safety == AI_ACTION_SAFETY::Modifying )
{
    deny( result, wxS( "requires_preview" ),
          wxS( "Modifying actions require preview and materialization policy." ) );
    return result;
}

if( aRequest.m_Action.m_Safety != AI_ACTION_SAFETY::ReadOnly
    && !IsAllowlisted( aRequest.m_Action.m_Name ) )
{
    deny( result, wxS( "not_allowlisted" ), wxS( "Action is not on the AI allowlist." ) );
    return result;
}

result.m_Allowed = true;
result.m_Executed = false;
result.m_Message = wxS( "Action is allowed." );
return result;
```

Keep invalid and disabled checks before this block.

- [x] **Step 2: Run GREEN verification**

Run the tool policy and handler verification command.

Expected: policy, handler, and runtime targeted suites pass.

## Task 3: Spec Index, Hygiene, And Commit

**Files:**
- Modify: `docs/superpowers/specs/2026-06-16-kisurf-ai-native-spec-index.md`

- [x] **Step 1: Update spec index**

Add:

```markdown
21. [AI Read-Only Action Policy](./2026-06-16-ai-readonly-action-policy-design.md)
   - Defines policy-level auto-allow behavior for enabled read-only actions.
```

Add implementation order:

```markdown
25. Phase 18 read-only action policy that lets models execute safe catalog actions without per-action allowlist plumbing.
```

- [x] **Step 2: Run diff hygiene checks**

Run:

```powershell
git diff --check
git status --short
git diff --stat
```

Expected: no whitespace errors and only files from this plan changed.

- [x] **Step 3: Commit**

```powershell
git add common/kisurf/ai/ai_tool_execution.cpp qa/tests/common/test_ai_tool_execution.cpp docs/superpowers/specs/2026-06-16-ai-readonly-action-policy-design.md docs/superpowers/specs/2026-06-16-kisurf-ai-native-spec-index.md docs/superpowers/plans/2026-06-16-ai-readonly-action-policy-implementation.md
git commit -m "feat: allow readonly ai actions"
```

## Plan Self-Review

- Spec coverage: tasks cover read-only auto-allow, interactive allowlist,
  modifying/destructive denial, executor behavior, and verification.
- Open-marker scan: no unresolved placeholders remain.
- Type consistency: test names, policy enum names, and error codes match the
  existing C++ code.
