# AI Validation Self Test Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add AI validation policy and a first self-test path that uses Computer Use before building native semantic-tree automation.

**Architecture:** Validation starts as a common policy and result-classification layer, then later plugs into PCB DRC and schematic ERC jobs. UI self-test is documented and trialed through Computer Use first; the native semantic-tree interface remains specified but outside this first implementation slice.

**Tech Stack:** C++17, KiCad DRC/ERC result concepts, Boost.Test, CMake QA targets, Codex Computer Use for manual/semi-automated UI smoke testing.

---

## File Structure

- Create: `include/kisurf/ai/ai_validation.h`
  - Validation scope, runner interface, diff classification, blocking policy.
- Create: `common/kisurf/ai/ai_validation.cpp`
  - Summary aggregation, before/after diff classification, policy decisions.
- Modify: `common/CMakeLists.txt:90`
  - Add `kisurf/ai/ai_validation.cpp`.
- Create: `qa/tests/common/test_ai_validation.cpp`
  - Unit tests for validation severity, new-vs-existing issue classification, and blocking policy.
- Modify: `qa/tests/common/CMakeLists.txt:24`
- Create: `docs/superpowers/plans/2026-06-16-agent-pane-computer-use-smoke-test.md`
  - Manual and Computer Use checklist for opening editors, showing Agent, sending a stub prompt, and verifying response.

## Task 1: Validation Policy

**Files:**
- Create: `include/kisurf/ai/ai_validation.h`
- Create: `common/kisurf/ai/ai_validation.cpp`
- Test: `qa/tests/common/test_ai_validation.cpp`
- Modify: `common/CMakeLists.txt:90`
- Modify: `qa/tests/common/CMakeLists.txt:24`

- [ ] **Step 1: Write failing validation tests**

Create `qa/tests/common/test_ai_validation.cpp` with:

```cpp
#include <boost/test/unit_test.hpp>
#include <kisurf/ai/ai_validation.h>

BOOST_AUTO_TEST_SUITE( AiValidation )

BOOST_AUTO_TEST_CASE( ChatOnlyScopeNeverBlocks )
{
    AI_VALIDATION_POLICY policy;

    AI_VALIDATION_SUMMARY summary;
    summary.m_Issues.push_back( { AI_VALIDATION_SEVERITY::Error, wxS( "existing issue" ), false } );

    BOOST_CHECK( !policy.BlocksApply( AI_VALIDATION_SCOPE::None, summary ) );
}

BOOST_AUTO_TEST_CASE( NewErrorBlocksApply )
{
    AI_VALIDATION_POLICY policy;

    AI_VALIDATION_SUMMARY summary;
    summary.m_Issues.push_back( { AI_VALIDATION_SEVERITY::Error, wxS( "new short" ), true } );

    BOOST_CHECK( policy.BlocksApply( AI_VALIDATION_SCOPE::LocalPreflight, summary ) );
}

BOOST_AUTO_TEST_CASE( ExistingErrorDoesNotBlockAiApply )
{
    AI_VALIDATION_POLICY policy;

    AI_VALIDATION_SUMMARY summary;
    summary.m_Issues.push_back( { AI_VALIDATION_SEVERITY::Error, wxS( "pre-existing short" ), false } );

    BOOST_CHECK( !policy.BlocksApply( AI_VALIDATION_SCOPE::PostApplyLocal, summary ) );
}

BOOST_AUTO_TEST_CASE( DiffMarksOnlyNewIssueAsNew )
{
    AI_VALIDATION_DIFF diff;

    diff.m_Before.push_back( { AI_VALIDATION_SEVERITY::Error, wxS( "old short" ), false } );
    diff.m_After.push_back( { AI_VALIDATION_SEVERITY::Error, wxS( "old short" ), false } );
    diff.m_After.push_back( { AI_VALIDATION_SEVERITY::Warning, wxS( "new clearance" ), false } );

    AI_VALIDATION_SUMMARY summary = diff.Classify();

    BOOST_REQUIRE_EQUAL( summary.m_Issues.size(), 2 );
    BOOST_CHECK( !summary.m_Issues.at( 0 ).m_IsNew );
    BOOST_CHECK( summary.m_Issues.at( 1 ).m_IsNew );
}

BOOST_AUTO_TEST_SUITE_END()
```

- [ ] **Step 2: Register failing validation test**

Add to `QA_COMMON_SRCS`:

```cmake
    test_ai_validation.cpp
```

Run:

```powershell
cmake --build --preset x64-release --target qa_common
```

Expected:

- Build fails because `<kisurf/ai/ai_validation.h>` does not exist yet.

- [ ] **Step 3: Add validation header**

Create `include/kisurf/ai/ai_validation.h` with:

```cpp
#pragma once

#include <import_export.h>
#include <kisurf/ai/ai_types.h>

enum class AI_VALIDATION_SCOPE
{
    None,
    LocalPreflight,
    PostApplyLocal,
    FullPcbDrc,
    FullSchErc,
    HeadlessBatch
};

class APIEXPORT AI_VALIDATION_DIFF
{
public:
    std::vector<AI_VALIDATION_ISSUE> m_Before;
    std::vector<AI_VALIDATION_ISSUE> m_After;

    AI_VALIDATION_SUMMARY Classify() const;
};

class APIEXPORT AI_VALIDATION_POLICY
{
public:
    bool BlocksApply( AI_VALIDATION_SCOPE aScope, const AI_VALIDATION_SUMMARY& aSummary ) const;
};
```

- [ ] **Step 4: Add validation implementation**

Create `common/kisurf/ai/ai_validation.cpp` with:

```cpp
#include <kisurf/ai/ai_validation.h>

namespace
{
bool sameIssue( const AI_VALIDATION_ISSUE& aLeft, const AI_VALIDATION_ISSUE& aRight )
{
    return aLeft.m_Severity == aRight.m_Severity && aLeft.m_Message == aRight.m_Message;
}
}


AI_VALIDATION_SUMMARY AI_VALIDATION_DIFF::Classify() const
{
    AI_VALIDATION_SUMMARY summary;

    for( AI_VALIDATION_ISSUE issue : m_After )
    {
        bool existedBefore = false;

        for( const AI_VALIDATION_ISSUE& before : m_Before )
        {
            if( sameIssue( before, issue ) )
            {
                existedBefore = true;
                break;
            }
        }

        issue.m_IsNew = !existedBefore;
        summary.m_Issues.push_back( issue );
    }

    return summary;
}


bool AI_VALIDATION_POLICY::BlocksApply( AI_VALIDATION_SCOPE aScope,
                                        const AI_VALIDATION_SUMMARY& aSummary ) const
{
    if( aScope == AI_VALIDATION_SCOPE::None )
        return false;

    return aSummary.HasBlockingIssue();
}
```

- [ ] **Step 5: Register validation source and run tests**

Add to `common/CMakeLists.txt`:

```cmake
    kisurf/ai/ai_validation.cpp
```

Run:

```powershell
cmake --build --preset x64-release --target qa_common
$env:KICAD_RUN_FROM_BUILD_DIR='1'; ctest --test-dir out/build/x64-release -R qa_common --output-on-failure
```

Expected:

- `AiValidation` tests pass.

- [ ] **Step 6: Commit validation policy**

Run:

```powershell
git add include/kisurf/ai/ai_validation.h common/kisurf/ai/ai_validation.cpp common/CMakeLists.txt qa/tests/common/CMakeLists.txt qa/tests/common/test_ai_validation.cpp
git commit -m "feat: add ai validation policy"
```

Expected:

- Commit succeeds.

## Task 2: Attach Validation To Edit Session

**Files:**
- Modify: `include/kisurf/ai/ai_edit_session.h`
- Modify: `common/kisurf/ai/ai_edit_session.cpp`
- Modify: `qa/tests/common/test_ai_edit_session.cpp`

- [ ] **Step 1: Extend edit session tests for blocking policy**

Add this test to `qa/tests/common/test_ai_edit_session.cpp`:

```cpp
BOOST_AUTO_TEST_CASE( BlockingValidationPreventsAdapterApply )
{
    FAKE_EDIT_ADAPTER adapter;
    AI_EDIT_SESSION session( adapter );
    AI_OBJECT_REF trace( KIID(), PCB_TRACE_T, wxS( "route-b" ) );

    AI_VALIDATION_SUMMARY validation;
    validation.m_Issues.push_back( { AI_VALIDATION_SEVERITY::Error, wxS( "new short" ), true } );

    BOOST_CHECK( !session.Apply( { trace }, validation ) );
    BOOST_CHECK( adapter.m_Applied.empty() );
}
```

Run:

```powershell
cmake --build --preset x64-release --target qa_common
$env:KICAD_RUN_FROM_BUILD_DIR='1'; ctest --test-dir out/build/x64-release -R qa_common --output-on-failure
```

Expected:

- Test passes because `AI_EDIT_SESSION::Apply` already blocks `HasBlockingIssue()`.

- [ ] **Step 2: Commit validation/edit integration test**

Run:

```powershell
git add qa/tests/common/test_ai_edit_session.cpp
git commit -m "test: cover ai edit validation blocking"
```

Expected:

- Commit succeeds.

## Task 3: Computer Use Smoke Test Checklist

**Files:**
- Create: `docs/superpowers/plans/2026-06-16-agent-pane-computer-use-smoke-test.md`

- [ ] **Step 1: Write the smoke checklist**

Create `docs/superpowers/plans/2026-06-16-agent-pane-computer-use-smoke-test.md` with:

```markdown
# Agent Pane Computer Use Smoke Test

Date: 2026-06-16

## Purpose

Verify the first Agent pane with Computer Use before investing in a native semantic-tree automation interface.

## Preconditions

- `pcbnew_kiface`, `eeschema_kiface`, and `kicad` build from `out/build/x64-release`.
- The Agent pane implementation is present.
- The stub provider is active.

## Manual Steps

1. Launch KiCad from `out/build/x64-release`.
2. Open a sample project.
3. Open PCB Editor.
4. Use View > Panels > Agent.
5. Confirm the Agent pane appears.
6. Type `hello from pcb`.
7. Press Send.
8. Confirm the transcript shows the user message and a `Stub Agent` response.
9. Open Schematic Editor.
10. Use View > Panels > Agent.
11. Type `hello from schematic`.
12. Press Send.
13. Confirm the transcript shows the user message and a `Stub Agent` response.

## Computer Use Trial Script

Use Computer Use to perform the same flow:

1. Identify the KiCad main window.
2. Open the PCB editor.
3. Click the View menu.
4. Click the Agent panel item.
5. Click the Agent input.
6. Enter `hello from computer use`.
7. Click Send.
8. Verify visible transcript text contains `Stub Agent`.

## Pass Criteria

- The pane can be shown in PCB and schematic editors.
- The input accepts text.
- Send appends a deterministic response.
- Computer Use can identify and click the relevant UI controls well enough for early smoke testing.

## Escalation Criteria

If Computer Use cannot reliably identify the Agent pane, menus, or Send button after two implementation iterations, create the native semantic-tree automation plan from the validation/self-test spec.
```

- [ ] **Step 2: Commit smoke checklist**

Run:

```powershell
git add docs/superpowers/plans/2026-06-16-agent-pane-computer-use-smoke-test.md
git commit -m "docs: add agent pane computer use smoke test"
```

Expected:

- Commit succeeds.

## Task 4: Native Semantic-Tree Decision Record

**Files:**
- Modify: `docs/superpowers/plans/2026-06-16-agent-pane-computer-use-smoke-test.md`

- [ ] **Step 1: Record the decision after the trial**

After running the Computer Use smoke test, append one of these exact decision records.

Use this block if Computer Use passes:

```markdown
## Trial Result

Computer Use passed the first Agent pane smoke test. Native semantic-tree automation remains deferred while KiSurf focuses on context, preview, edit, and validation workflows.
```

Use this block if Computer Use fails twice on stable UI:

```markdown
## Trial Result

Computer Use failed the first Agent pane smoke test twice on stable UI. The next planning step is a native semantic-tree automation implementation plan with screenshot capture, node enumeration, node bounds, node action invocation, text fill, and privacy filtering.
```

- [ ] **Step 2: Commit the recorded trial result**

Run:

```powershell
git add docs/superpowers/plans/2026-06-16-agent-pane-computer-use-smoke-test.md
git commit -m "docs: record agent pane computer use result"
```

Expected:

- Commit succeeds.

## Acceptance Criteria

- Validation policy tests pass in `qa_common`.
- Blocking validation prevents edit-session adapter apply.
- Computer Use smoke checklist exists before native semantic-tree automation work begins.
- Native semantic-tree automation remains a decision-gated follow-up, not part of the first implementation slice.
