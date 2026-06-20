# AI Tool Execution Activity Trace Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a safe, testable gate for model-requested KiSurf tool calls and a bounded in-memory activity trace for user/model actions.

**Architecture:** Shared value types stay in `ai_types`; activity logging and policy/execution live in small kicommon modules with fake-runner tests. Editor-specific `TOOL_MANAGER` integration remains a later adapter layer, so this phase proves the safety and audit boundary without directly mutating KiCad documents.

**Tech Stack:** C++17, wxWidgets strings, KiSurf AI common types, Boost.Test, CMake/Ninja, `qa_common`.

---

## File Structure

- Modify: `include/kisurf/ai/ai_types.h`
  - Add `AI_ACTIVITY_KIND`, `AI_ACTIVITY_RECORD`, `AI_TOOL_INVOCATION_REQUEST`, and `AI_TOOL_INVOCATION_RESULT`.
- Modify: `common/kisurf/ai/ai_types.cpp`
  - Add small formatting helpers for tool invocation results.
- Create: `include/kisurf/ai/ai_activity_log.h`
  - Bounded in-memory log API.
- Create: `common/kisurf/ai/ai_activity_log.cpp`
  - Sequence assignment and capacity trimming.
- Create: `include/kisurf/ai/ai_tool_execution.h`
  - Policy, runner interface, and executor API.
- Create: `common/kisurf/ai/ai_tool_execution.cpp`
  - Deny-by-default policy and audited execution flow.
- Modify: `common/CMakeLists.txt`
  - Add `kisurf/ai/ai_activity_log.cpp` and `kisurf/ai/ai_tool_execution.cpp`.
- Create: `qa/tests/common/test_ai_activity_log.cpp`
- Create: `qa/tests/common/test_ai_tool_execution.cpp`
- Modify: `qa/tests/common/CMakeLists.txt`
  - Add both tests to `QA_COMMON_SRCS`.

## Task 1: Tool And Activity Types

**Files:**
- Modify: `include/kisurf/ai/ai_types.h`
- Modify: `common/kisurf/ai/ai_types.cpp`
- Modify: `qa/tests/common/test_ai_types.cpp`

- [ ] **Step 1: Write failing type tests**

Add to `qa/tests/common/test_ai_types.cpp`:

```cpp
BOOST_AUTO_TEST_CASE( ToolInvocationResultFormatsStatus )
{
    AI_TOOL_INVOCATION_RESULT result;
    result.m_RequestId = 7;
    result.m_ToolCallId = wxS( "call_1" );
    result.m_ActionName = wxS( "common.Control.showAgentPanel" );
    result.m_Allowed = true;
    result.m_Executed = true;
    result.m_Message = wxS( "ran" );

    wxString text = result.AsTraceText();

    BOOST_CHECK( text.Contains( wxS( "request=7" ) ) );
    BOOST_CHECK( text.Contains( wxS( "tool_call=call_1" ) ) );
    BOOST_CHECK( text.Contains( wxS( "allowed=yes" ) ) );
    BOOST_CHECK( text.Contains( wxS( "executed=yes" ) ) );
}
```

- [ ] **Step 2: Run test to verify RED**

Run:

```powershell
cmake --build out/build/x64-release --target qa_common -- -j 2
```

Expected: build fails because `AI_TOOL_INVOCATION_RESULT` or `AsTraceText()` is not defined.

- [ ] **Step 3: Add type declarations**

Add to `include/kisurf/ai/ai_types.h` after `AI_TOOL_CALL_RECORD`:

```cpp
enum class AI_ACTIVITY_KIND
{
    UserAction,
    ModelToolRequest,
    PolicyDecision,
    ToolResult
};

struct KICOMMON_API AI_ACTIVITY_RECORD
{
    uint64_t         m_Sequence = 0;
    uint64_t         m_RequestId = 0;
    wxString         m_ToolCallId;
    AI_ACTIVITY_KIND m_Kind = AI_ACTIVITY_KIND::UserAction;
    AI_EDITOR_KIND   m_EditorKind = AI_EDITOR_KIND::Unknown;
    wxString         m_ActionName;
    wxString         m_ArgumentsJson;
    wxString         m_ResultJson;
    bool             m_Allowed = false;
    bool             m_Executed = false;
    wxString         m_Message;
};

struct KICOMMON_API AI_TOOL_INVOCATION_REQUEST
{
    uint64_t             m_RequestId = 0;
    wxString             m_ToolCallId;
    AI_EDITOR_KIND       m_EditorKind = AI_EDITOR_KIND::Unknown;
    AI_CONTEXT_VERSION   m_ContextVersion;
    AI_ACTION_DESCRIPTOR m_Action;
    wxString             m_ArgumentsJson;
    bool                 m_DryRun = false;
    bool                 m_UserAccepted = false;
};

struct KICOMMON_API AI_TOOL_INVOCATION_RESULT
{
    uint64_t m_RequestId = 0;
    wxString m_ToolCallId;
    wxString m_ActionName;
    bool     m_Allowed = false;
    bool     m_Executed = false;
    wxString m_ErrorCode;
    wxString m_Message;
    wxString m_ResultJson;

    wxString AsTraceText() const;
};
```

- [ ] **Step 4: Add formatter implementation**

Add to `common/kisurf/ai/ai_types.cpp`:

```cpp
wxString AI_TOOL_INVOCATION_RESULT::AsTraceText() const
{
    return wxString::Format( wxS( "request=%llu tool_call=%s action=%s allowed=%s executed=%s error=%s message=%s" ),
                             static_cast<unsigned long long>( m_RequestId ),
                             m_ToolCallId,
                             m_ActionName,
                             m_Allowed ? wxS( "yes" ) : wxS( "no" ),
                             m_Executed ? wxS( "yes" ) : wxS( "no" ),
                             m_ErrorCode,
                             m_Message );
}
```

- [ ] **Step 5: Verify GREEN**

Run:

```powershell
cmake --build out/build/x64-release --target qa_common -- -j 2
$env:KICAD_RUN_FROM_BUILD_DIR='1'; $env:KICAD_BUILD_PATHS='C:/Users/wenming/source/repos/kisurf/out/build/x64-release/kicad:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/common:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/api:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/common/gal:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/pcbnew:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/eeschema:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/cvpcb'; $env:PATH='D:\Tools\vcpkg\installed\x64-windows\bin;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\kicad;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\common;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\api;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\common\gal;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\pcbnew;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\eeschema;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\cvpcb;' + $env:PATH; out\build\x64-release\qa\tests\common\qa_common.exe --run_test=AiNativeTypes --log_level=test_suite
```

Expected: build exits 0; `AiNativeTypes` exits 0.

## Task 2: Bounded Activity Log

**Files:**
- Create: `include/kisurf/ai/ai_activity_log.h`
- Create: `common/kisurf/ai/ai_activity_log.cpp`
- Create: `qa/tests/common/test_ai_activity_log.cpp`
- Modify: `common/CMakeLists.txt`
- Modify: `qa/tests/common/CMakeLists.txt`

- [ ] **Step 1: Write failing activity log tests**

Create `qa/tests/common/test_ai_activity_log.cpp`:

```cpp
#include <boost/test/unit_test.hpp>

#include <kisurf/ai/ai_activity_log.h>

BOOST_AUTO_TEST_SUITE( AiActivityLog )

BOOST_AUTO_TEST_CASE( AppendAssignsIncreasingSequenceNumbers )
{
    AI_ACTIVITY_LOG log( 8 );

    AI_ACTIVITY_RECORD first;
    first.m_ActionName = wxS( "one" );

    AI_ACTIVITY_RECORD second;
    second.m_ActionName = wxS( "two" );

    BOOST_CHECK_EQUAL( log.Append( first ).m_Sequence, 1 );
    BOOST_CHECK_EQUAL( log.Append( second ).m_Sequence, 2 );

    std::vector<AI_ACTIVITY_RECORD> records = log.Records();
    BOOST_REQUIRE_EQUAL( records.size(), 2 );
    BOOST_CHECK_EQUAL( records.at( 0 ).m_ActionName, wxString( wxS( "one" ) ) );
    BOOST_CHECK_EQUAL( records.at( 1 ).m_ActionName, wxString( wxS( "two" ) ) );
}

BOOST_AUTO_TEST_CASE( LogKeepsOnlyConfiguredCapacity )
{
    AI_ACTIVITY_LOG log( 2 );

    for( int i = 0; i < 3; ++i )
    {
        AI_ACTIVITY_RECORD record;
        record.m_ActionName = wxString::Format( wxS( "action-%d" ), i );
        log.Append( record );
    }

    std::vector<AI_ACTIVITY_RECORD> records = log.Records();
    BOOST_REQUIRE_EQUAL( records.size(), 2 );
    BOOST_CHECK_EQUAL( records.at( 0 ).m_Sequence, 2 );
    BOOST_CHECK_EQUAL( records.at( 1 ).m_Sequence, 3 );
    BOOST_CHECK_EQUAL( records.at( 0 ).m_ActionName, wxString( wxS( "action-1" ) ) );
}

BOOST_AUTO_TEST_SUITE_END()
```

- [ ] **Step 2: Register failing test and source**

Add to `qa/tests/common/CMakeLists.txt`:

```cmake
    test_ai_activity_log.cpp
```

Add to `common/CMakeLists.txt` in the KiSurf AI Native block:

```cmake
    kisurf/ai/ai_activity_log.cpp
```

Run:

```powershell
cmake --build out/build/x64-release --target qa_common -- -j 2
```

Expected: build fails because `<kisurf/ai/ai_activity_log.h>` does not exist.

- [ ] **Step 3: Add activity log header**

Create `include/kisurf/ai/ai_activity_log.h`:

```cpp
#pragma once

#include <kicommon.h>
#include <kisurf/ai/ai_types.h>

#include <cstddef>
#include <mutex>
#include <vector>

class KICOMMON_API AI_ACTIVITY_LOG
{
public:
    explicit AI_ACTIVITY_LOG( size_t aCapacity = 256 );

    AI_ACTIVITY_RECORD Append( AI_ACTIVITY_RECORD aRecord );
    std::vector<AI_ACTIVITY_RECORD> Records() const;
    void Clear();

private:
    size_t                          m_Capacity = 0;
    uint64_t                        m_NextSequence = 1;
    mutable std::mutex              m_Mutex;
    std::vector<AI_ACTIVITY_RECORD> m_Records;
};
```

- [ ] **Step 4: Add activity log implementation**

Create `common/kisurf/ai/ai_activity_log.cpp`:

```cpp
#include <kisurf/ai/ai_activity_log.h>

AI_ACTIVITY_LOG::AI_ACTIVITY_LOG( size_t aCapacity ) :
        m_Capacity( aCapacity )
{
}


AI_ACTIVITY_RECORD AI_ACTIVITY_LOG::Append( AI_ACTIVITY_RECORD aRecord )
{
    std::lock_guard<std::mutex> lock( m_Mutex );

    aRecord.m_Sequence = m_NextSequence++;

    if( m_Capacity > 0 )
    {
        m_Records.push_back( aRecord );

        while( m_Records.size() > m_Capacity )
            m_Records.erase( m_Records.begin() );
    }

    return aRecord;
}


std::vector<AI_ACTIVITY_RECORD> AI_ACTIVITY_LOG::Records() const
{
    std::lock_guard<std::mutex> lock( m_Mutex );
    return m_Records;
}


void AI_ACTIVITY_LOG::Clear()
{
    std::lock_guard<std::mutex> lock( m_Mutex );
    m_Records.clear();
}
```

- [ ] **Step 5: Verify GREEN**

Run:

```powershell
cmake --build out/build/x64-release --target qa_common -- -j 2
$env:KICAD_RUN_FROM_BUILD_DIR='1'; $env:KICAD_BUILD_PATHS='C:/Users/wenming/source/repos/kisurf/out/build/x64-release/kicad:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/common:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/api:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/common/gal:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/pcbnew:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/eeschema:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/cvpcb'; $env:PATH='D:\Tools\vcpkg\installed\x64-windows\bin;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\kicad;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\common;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\api;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\common\gal;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\pcbnew;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\eeschema;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\cvpcb;' + $env:PATH; out\build\x64-release\qa\tests\common\qa_common.exe --run_test=AiActivityLog --log_level=test_suite
```

Expected: `AiActivityLog` exits 0.

## Task 3: Tool Policy And Executor

**Files:**
- Create: `include/kisurf/ai/ai_tool_execution.h`
- Create: `common/kisurf/ai/ai_tool_execution.cpp`
- Create: `qa/tests/common/test_ai_tool_execution.cpp`
- Modify: `common/CMakeLists.txt`
- Modify: `qa/tests/common/CMakeLists.txt`

- [ ] **Step 1: Write failing executor tests**

Create `qa/tests/common/test_ai_tool_execution.cpp`:

```cpp
#include <boost/test/unit_test.hpp>

#include <kisurf/ai/ai_activity_log.h>
#include <kisurf/ai/ai_tool_execution.h>

class FAKE_ACTION_RUNNER : public AI_ACTION_RUNNER
{
public:
    bool RunActionByName( const wxString& aActionName, wxString& aError ) override
    {
        m_Calls.push_back( aActionName );

        if( !m_ShouldSucceed )
        {
            aError = wxS( "runner failed" );
            return false;
        }

        return true;
    }

    bool                  m_ShouldSucceed = true;
    std::vector<wxString> m_Calls;
};

static AI_ACTION_DESCRIPTOR actionDescriptor( const wxString& aName, AI_ACTION_SAFETY aSafety,
                                              bool aEnabled = true )
{
    AI_ACTION_DESCRIPTOR descriptor;
    descriptor.m_Name = aName;
    descriptor.m_FriendlyName = aName;
    descriptor.m_EditorKind = AI_EDITOR_KIND::Pcb;
    descriptor.m_Safety = aSafety;
    descriptor.m_Enabled = aEnabled;
    return descriptor;
}

BOOST_AUTO_TEST_SUITE( AiToolExecution )

BOOST_AUTO_TEST_CASE( PolicyAllowsAllowlistedReadonlyAction )
{
    AI_TOOL_EXECUTION_POLICY policy;
    policy.AllowAction( wxS( "common.Control.showAgentPanel" ) );

    AI_TOOL_INVOCATION_REQUEST request;
    request.m_Action = actionDescriptor( wxS( "common.Control.showAgentPanel" ),
                                         AI_ACTION_SAFETY::ReadOnly );

    AI_TOOL_INVOCATION_RESULT result = policy.Evaluate( request );

    BOOST_CHECK( result.m_Allowed );
    BOOST_CHECK( result.m_ErrorCode.IsEmpty() );
}

BOOST_AUTO_TEST_CASE( PolicyDeniesUnsafeActions )
{
    AI_TOOL_EXECUTION_POLICY policy;
    policy.AllowAction( wxS( "common.Control.showAgentPanel" ) );
    policy.AllowAction( wxS( "pcbnew.Edit.delete" ) );

    AI_TOOL_INVOCATION_REQUEST unknown;
    unknown.m_Action = AI_ACTION_DESCRIPTOR();
    BOOST_CHECK_EQUAL( policy.Evaluate( unknown ).m_ErrorCode,
                       wxString( wxS( "unknown_action" ) ) );

    AI_TOOL_INVOCATION_REQUEST notAllowlisted;
    notAllowlisted.m_Action = actionDescriptor( wxS( "pcbnew.Control.zoomFitScreen" ),
                                                AI_ACTION_SAFETY::ReadOnly );
    BOOST_CHECK_EQUAL( policy.Evaluate( notAllowlisted ).m_ErrorCode,
                       wxString( wxS( "not_allowlisted" ) ) );

    AI_TOOL_INVOCATION_REQUEST disabled;
    disabled.m_Action = actionDescriptor( wxS( "common.Control.showAgentPanel" ),
                                          AI_ACTION_SAFETY::ReadOnly, false );
    BOOST_CHECK_EQUAL( policy.Evaluate( disabled ).m_ErrorCode,
                       wxString( wxS( "disabled_action" ) ) );

    AI_TOOL_INVOCATION_REQUEST modifying;
    modifying.m_Action = actionDescriptor( wxS( "pcbnew.Place.move" ),
                                           AI_ACTION_SAFETY::Modifying );
    policy.AllowAction( modifying.m_Action.m_Name );
    BOOST_CHECK_EQUAL( policy.Evaluate( modifying ).m_ErrorCode,
                       wxString( wxS( "requires_preview" ) ) );

    AI_TOOL_INVOCATION_REQUEST destructive;
    destructive.m_Action = actionDescriptor( wxS( "pcbnew.Edit.delete" ),
                                             AI_ACTION_SAFETY::Destructive );
    BOOST_CHECK_EQUAL( policy.Evaluate( destructive ).m_ErrorCode,
                       wxString( wxS( "destructive_denied" ) ) );
}

BOOST_AUTO_TEST_CASE( ExecutorAuditsAndRunsOnlyAllowedCalls )
{
    AI_ACTIVITY_LOG          log( 16 );
    AI_TOOL_EXECUTION_POLICY policy;
    FAKE_ACTION_RUNNER       runner;

    policy.AllowAction( wxS( "common.Control.showAgentPanel" ) );
    AI_TOOL_EXECUTOR executor( policy, runner, log );

    AI_TOOL_INVOCATION_REQUEST request;
    request.m_RequestId = 9;
    request.m_ToolCallId = wxS( "call_9" );
    request.m_Action = actionDescriptor( wxS( "common.Control.showAgentPanel" ),
                                         AI_ACTION_SAFETY::ReadOnly );

    AI_TOOL_INVOCATION_RESULT result = executor.Invoke( request );

    BOOST_CHECK( result.m_Allowed );
    BOOST_CHECK( result.m_Executed );
    BOOST_REQUIRE_EQUAL( runner.m_Calls.size(), 1 );
    BOOST_CHECK_EQUAL( runner.m_Calls.front(),
                       wxString( wxS( "common.Control.showAgentPanel" ) ) );

    std::vector<AI_ACTIVITY_RECORD> records = log.Records();
    BOOST_REQUIRE_EQUAL( records.size(), 3 );
    BOOST_CHECK( records.at( 0 ).m_Kind == AI_ACTIVITY_KIND::ModelToolRequest );
    BOOST_CHECK( records.at( 1 ).m_Kind == AI_ACTIVITY_KIND::PolicyDecision );
    BOOST_CHECK( records.at( 2 ).m_Kind == AI_ACTIVITY_KIND::ToolResult );
}

BOOST_AUTO_TEST_CASE( ExecutorDoesNotRunDeniedOrDryRunCalls )
{
    AI_ACTIVITY_LOG          log( 16 );
    AI_TOOL_EXECUTION_POLICY policy;
    FAKE_ACTION_RUNNER       runner;
    AI_TOOL_EXECUTOR         executor( policy, runner, log );

    AI_TOOL_INVOCATION_REQUEST denied;
    denied.m_Action = actionDescriptor( wxS( "common.Control.showAgentPanel" ),
                                        AI_ACTION_SAFETY::ReadOnly );

    AI_TOOL_INVOCATION_RESULT deniedResult = executor.Invoke( denied );
    BOOST_CHECK( !deniedResult.m_Allowed );
    BOOST_CHECK( !deniedResult.m_Executed );
    BOOST_CHECK( runner.m_Calls.empty() );

    policy.AllowAction( wxS( "common.Control.showAgentPanel" ) );

    AI_TOOL_INVOCATION_REQUEST dryRun;
    dryRun.m_Action = actionDescriptor( wxS( "common.Control.showAgentPanel" ),
                                        AI_ACTION_SAFETY::ReadOnly );
    dryRun.m_DryRun = true;

    AI_TOOL_INVOCATION_RESULT dryRunResult = executor.Invoke( dryRun );
    BOOST_CHECK( dryRunResult.m_Allowed );
    BOOST_CHECK( !dryRunResult.m_Executed );
    BOOST_CHECK( runner.m_Calls.empty() );
}

BOOST_AUTO_TEST_SUITE_END()
```

- [ ] **Step 2: Register failing source and test**

Add to `qa/tests/common/CMakeLists.txt`:

```cmake
    test_ai_tool_execution.cpp
```

Add to `common/CMakeLists.txt` in the KiSurf AI Native block:

```cmake
    kisurf/ai/ai_tool_execution.cpp
```

Run:

```powershell
cmake --build out/build/x64-release --target qa_common -- -j 2
```

Expected: build fails because `<kisurf/ai/ai_tool_execution.h>` does not exist.

- [ ] **Step 3: Add tool execution header**

Create `include/kisurf/ai/ai_tool_execution.h`:

```cpp
#pragma once

#include <kicommon.h>
#include <kisurf/ai/ai_activity_log.h>
#include <kisurf/ai/ai_types.h>

#include <set>

class KICOMMON_API AI_ACTION_RUNNER
{
public:
    virtual ~AI_ACTION_RUNNER() = default;

    virtual bool RunActionByName( const wxString& aActionName, wxString& aError ) = 0;
};

class KICOMMON_API AI_TOOL_EXECUTION_POLICY
{
public:
    void AllowAction( const wxString& aActionName );
    bool IsAllowlisted( const wxString& aActionName ) const;

    AI_TOOL_INVOCATION_RESULT Evaluate( const AI_TOOL_INVOCATION_REQUEST& aRequest ) const;

private:
    std::set<wxString> m_Allowlist;
};

class KICOMMON_API AI_TOOL_EXECUTOR
{
public:
    AI_TOOL_EXECUTOR( const AI_TOOL_EXECUTION_POLICY& aPolicy,
                      AI_ACTION_RUNNER& aRunner,
                      AI_ACTIVITY_LOG& aActivityLog );

    AI_TOOL_INVOCATION_RESULT Invoke( const AI_TOOL_INVOCATION_REQUEST& aRequest );

private:
    void recordRequest( const AI_TOOL_INVOCATION_REQUEST& aRequest );
    void recordResult( AI_ACTIVITY_KIND aKind, const AI_TOOL_INVOCATION_RESULT& aResult );

    const AI_TOOL_EXECUTION_POLICY& m_Policy;
    AI_ACTION_RUNNER&               m_Runner;
    AI_ACTIVITY_LOG&                m_ActivityLog;
};
```

- [ ] **Step 4: Add tool execution implementation**

Create `common/kisurf/ai/ai_tool_execution.cpp`:

```cpp
#include <kisurf/ai/ai_tool_execution.h>

namespace
{
AI_TOOL_INVOCATION_RESULT makeResult( const AI_TOOL_INVOCATION_REQUEST& aRequest )
{
    AI_TOOL_INVOCATION_RESULT result;
    result.m_RequestId = aRequest.m_RequestId;
    result.m_ToolCallId = aRequest.m_ToolCallId;
    result.m_ActionName = aRequest.m_Action.m_Name;
    return result;
}


void deny( AI_TOOL_INVOCATION_RESULT& aResult, const wxString& aCode,
           const wxString& aMessage )
{
    aResult.m_Allowed = false;
    aResult.m_Executed = false;
    aResult.m_ErrorCode = aCode;
    aResult.m_Message = aMessage;
}
} // namespace


void AI_TOOL_EXECUTION_POLICY::AllowAction( const wxString& aActionName )
{
    if( !aActionName.IsEmpty() )
        m_Allowlist.insert( aActionName );
}


bool AI_TOOL_EXECUTION_POLICY::IsAllowlisted( const wxString& aActionName ) const
{
    return m_Allowlist.find( aActionName ) != m_Allowlist.end();
}


AI_TOOL_INVOCATION_RESULT AI_TOOL_EXECUTION_POLICY::Evaluate(
        const AI_TOOL_INVOCATION_REQUEST& aRequest ) const
{
    AI_TOOL_INVOCATION_RESULT result = makeResult( aRequest );

    if( !aRequest.m_Action.IsValid() )
    {
        deny( result, wxS( "unknown_action" ), wxS( "Action descriptor is not valid." ) );
        return result;
    }

    if( !aRequest.m_Action.m_Enabled )
    {
        deny( result, wxS( "disabled_action" ), wxS( "Action is not currently enabled." ) );
        return result;
    }

    if( !IsAllowlisted( aRequest.m_Action.m_Name ) )
    {
        deny( result, wxS( "not_allowlisted" ), wxS( "Action is not on the AI allowlist." ) );
        return result;
    }

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

    result.m_Allowed = true;
    result.m_Executed = false;
    result.m_Message = wxS( "Action is allowed." );
    return result;
}


AI_TOOL_EXECUTOR::AI_TOOL_EXECUTOR( const AI_TOOL_EXECUTION_POLICY& aPolicy,
                                    AI_ACTION_RUNNER& aRunner,
                                    AI_ACTIVITY_LOG& aActivityLog ) :
        m_Policy( aPolicy ),
        m_Runner( aRunner ),
        m_ActivityLog( aActivityLog )
{
}


AI_TOOL_INVOCATION_RESULT AI_TOOL_EXECUTOR::Invoke(
        const AI_TOOL_INVOCATION_REQUEST& aRequest )
{
    recordRequest( aRequest );

    AI_TOOL_INVOCATION_RESULT result = m_Policy.Evaluate( aRequest );
    recordResult( AI_ACTIVITY_KIND::PolicyDecision, result );

    if( !result.m_Allowed )
    {
        recordResult( AI_ACTIVITY_KIND::ToolResult, result );
        return result;
    }

    if( aRequest.m_DryRun )
    {
        result.m_Executed = false;
        result.m_Message = wxS( "Dry run allowed." );
        result.m_ResultJson = wxS( "{\"dry_run\":true}" );
        recordResult( AI_ACTIVITY_KIND::ToolResult, result );
        return result;
    }

    wxString error;

    if( m_Runner.RunActionByName( aRequest.m_Action.m_Name, error ) )
    {
        result.m_Executed = true;
        result.m_Message = wxS( "Action executed." );
        result.m_ResultJson = wxS( "{\"status\":\"executed\"}" );
    }
    else
    {
        result.m_Executed = false;
        result.m_ErrorCode = wxS( "runner_failed" );
        result.m_Message = error.IsEmpty() ? wxString( wxS( "Runner failed." ) ) : error;
    }

    recordResult( AI_ACTIVITY_KIND::ToolResult, result );
    return result;
}


void AI_TOOL_EXECUTOR::recordRequest( const AI_TOOL_INVOCATION_REQUEST& aRequest )
{
    AI_ACTIVITY_RECORD record;
    record.m_RequestId = aRequest.m_RequestId;
    record.m_ToolCallId = aRequest.m_ToolCallId;
    record.m_Kind = AI_ACTIVITY_KIND::ModelToolRequest;
    record.m_EditorKind = aRequest.m_EditorKind;
    record.m_ActionName = aRequest.m_Action.m_Name;
    record.m_ArgumentsJson = aRequest.m_ArgumentsJson;
    record.m_Message = aRequest.m_DryRun ? wxS( "dry run" ) : wxS( "execute" );
    m_ActivityLog.Append( record );
}


void AI_TOOL_EXECUTOR::recordResult( AI_ACTIVITY_KIND aKind,
                                     const AI_TOOL_INVOCATION_RESULT& aResult )
{
    AI_ACTIVITY_RECORD record;
    record.m_RequestId = aResult.m_RequestId;
    record.m_ToolCallId = aResult.m_ToolCallId;
    record.m_Kind = aKind;
    record.m_ActionName = aResult.m_ActionName;
    record.m_ResultJson = aResult.m_ResultJson;
    record.m_Allowed = aResult.m_Allowed;
    record.m_Executed = aResult.m_Executed;
    record.m_Message = aResult.m_Message;
    m_ActivityLog.Append( record );
}
```

- [ ] **Step 5: Verify GREEN**

Run:

```powershell
cmake --build out/build/x64-release --target qa_common -- -j 2
$env:KICAD_RUN_FROM_BUILD_DIR='1'; $env:KICAD_BUILD_PATHS='C:/Users/wenming/source/repos/kisurf/out/build/x64-release/kicad:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/common:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/api:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/common/gal:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/pcbnew:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/eeschema:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/cvpcb'; $env:PATH='D:\Tools\vcpkg\installed\x64-windows\bin;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\kicad;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\common;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\api;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\common\gal;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\pcbnew;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\eeschema;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\cvpcb;' + $env:PATH; out\build\x64-release\qa\tests\common\qa_common.exe --run_test=AiNativeTypes,AiActivityLog,AiToolExecution --log_level=test_suite
```

Expected: targeted tests exit 0.

## Task 4: Final Verification And Commit

**Files:**
- All files from Tasks 1-3.

- [ ] **Step 1: Run focused AI common tests**

Run:

```powershell
$env:KICAD_RUN_FROM_BUILD_DIR='1'; $env:KICAD_BUILD_PATHS='C:/Users/wenming/source/repos/kisurf/out/build/x64-release/kicad:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/common:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/api:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/common/gal:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/pcbnew:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/eeschema:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/cvpcb'; $env:PATH='D:\Tools\vcpkg\installed\x64-windows\bin;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\kicad;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\common;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\api;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\common\gal;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\pcbnew;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\eeschema;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\cvpcb;' + $env:PATH; out\build\x64-release\qa\tests\common\qa_common.exe --run_test=AiNativeTypes,AiActivityLog,AiToolExecution --log_level=test_suite
```

Expected: exit 0 and `*** No errors detected`.

- [ ] **Step 2: Run build target**

Run:

```powershell
cmake --build out/build/x64-release --target qa_common -- -j 2
```

Expected: exit 0.

- [ ] **Step 3: Run diff hygiene**

Run:

```powershell
git diff --check
git status --short
```

Expected: no whitespace errors; only intended Phase 3 files changed.

- [ ] **Step 4: Commit**

Run:

```powershell
git add include/kisurf/ai/ai_types.h common/kisurf/ai/ai_types.cpp include/kisurf/ai/ai_activity_log.h common/kisurf/ai/ai_activity_log.cpp include/kisurf/ai/ai_tool_execution.h common/kisurf/ai/ai_tool_execution.cpp common/CMakeLists.txt qa/tests/common/CMakeLists.txt qa/tests/common/test_ai_types.cpp qa/tests/common/test_ai_activity_log.cpp qa/tests/common/test_ai_tool_execution.cpp
git commit -m "feat: add ai tool execution activity trace"
```

Expected: commit succeeds.

## Acceptance Criteria

- Phase 3 spec is committed before implementation.
- This plan is committed before implementation.
- `AiNativeTypes`, `AiActivityLog`, and `AiToolExecution` targeted tests pass.
- `qa_common` target builds.
- Denied and dry-run tool calls do not call the runner.
- Allowlisted read-only tool calls call the runner exactly once and append audit records.
