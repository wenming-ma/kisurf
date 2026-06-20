# AI Agent Observability Log Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a derived Agent log surface that shows model input, tool calls, tool results, model output, and background suggestion lifecycle without duplicating runtime state.

**Architecture:** `AI_ACTIVITY_LOG`, `AI_TRACE_RECORD`, and suggestion records remain the fact sources. A new common-layer formatter derives bounded `AI_AGENT_OBSERVABILITY_ENTRY` rows for tests, `AI_AGENT_PANEL_MODEL`, and the Agent pane Log view. The UI renders those rows as compact text first; richer cards can be layered later without changing the model contract.

**Tech Stack:** C++17, wxWidgets, KiCad common/kicommon AI modules, Boost unit tests, `json_common.h`/nlohmann JSON.

---

## File Structure

- Create `include/kisurf/ai/ai_observability_log.h`
  - Defines `AI_AGENT_OBSERVABILITY_KIND`, `AI_AGENT_OBSERVABILITY_ENTRY`, and `AI_AGENT_OBSERVABILITY_LOG`.
- Create `common/kisurf/ai/ai_observability_log.cpp`
  - Implements trace/activity/suggestion merging, bounded output, redaction, and visual snapshot summarization.
- Create `qa/tests/common/test_ai_observability_log.cpp`
  - Unit tests for formatter behavior.
- Modify `common/CMakeLists.txt`
  - Add `kisurf/ai/ai_observability_log.cpp` to `KICOMMON_SRCS`.
- Modify `qa/tests/common/CMakeLists.txt`
  - Add `test_ai_observability_log.cpp` to `QA_COMMON_SRCS`.
- Modify `include/kisurf/ai/ai_agent_panel_model.h`
  - Include the formatter header and expose `ObservabilityEntries(size_t)`.
- Modify `common/kisurf/ai/ai_agent_panel_model.cpp`
  - Build derived entries from runtime traces, activity records, and suggestions.
- Modify `qa/tests/common/test_ai_agent_panel_model.cpp`
  - Add model-level tests for the new API.
- Modify `include/kisurf/ai/ai_agent_panel.h`
  - Add `RefreshLog()`, a log text formatter helper, and private UI fields for a Log tab.
- Modify `common/kisurf/ai/ai_agent_panel.cpp`
  - Add the Log tab/view and refresh it after send, suggestion updates, preview, accept, reject, and activity.
- Modify `qa/tests/common/test_ai_agent_panel.cpp`
  - Add surface tests for `RefreshLog()` and log entry text formatting.

## Task 1: Formatter Contract And Basic Trace Entries

**Files:**
- Create: `include/kisurf/ai/ai_observability_log.h`
- Create: `common/kisurf/ai/ai_observability_log.cpp`
- Create: `qa/tests/common/test_ai_observability_log.cpp`
- Modify: `common/CMakeLists.txt`
- Modify: `qa/tests/common/CMakeLists.txt`

- [x] **Step 1: Write the failing formatter test**

Add `qa/tests/common/test_ai_observability_log.cpp` with this first test:

```cpp
#include <boost/test/unit_test.hpp>
#include <kisurf/ai/ai_observability_log.h>

BOOST_AUTO_TEST_SUITE( AiAgentObservabilityLog )

BOOST_AUTO_TEST_CASE( TraceProducesModelInputAndOutputEntries )
{
    AI_TRACE_RECORD trace;
    trace.m_RequestId = 42;
    trace.m_Request.m_EditorKind = AI_EDITOR_KIND::Pcb;
    trace.m_Request.m_UserText = wxS( "route the selected net" );
    trace.m_Request.m_ContextSnapshot.m_EditorKind = AI_EDITOR_KIND::Pcb;
    trace.m_Request.m_ContextSnapshot.m_Summary = wxS( "2 selected pads" );
    trace.m_Response.m_RequestId = 42;
    trace.m_Response.m_Title = wxS( "Routing assistant" );
    trace.m_Response.m_Body = wxS( "I can preview the next segment." );

    AI_AGENT_OBSERVABILITY_LOG log;
    std::vector<AI_AGENT_OBSERVABILITY_ENTRY> entries =
            log.Build( { trace }, {}, {}, 16 );

    BOOST_REQUIRE_EQUAL( entries.size(), 2 );
    BOOST_CHECK( entries.at( 0 ).m_Kind == AI_AGENT_OBSERVABILITY_KIND::ModelInput );
    BOOST_CHECK_EQUAL( entries.at( 0 ).m_RequestId, 42 );
    BOOST_CHECK( entries.at( 0 ).m_Summary.Contains( wxS( "route the selected net" ) ) );
    BOOST_CHECK( entries.at( 0 ).m_DetailsJson.Contains( wxS( "2 selected pads" ) ) );
    BOOST_CHECK( entries.at( 1 ).m_Kind == AI_AGENT_OBSERVABILITY_KIND::ModelOutput );
    BOOST_CHECK( entries.at( 1 ).m_Summary.Contains( wxS( "preview the next segment" ) ) );
}

BOOST_AUTO_TEST_SUITE_END()
```

- [x] **Step 2: Register the test and run red**

Modify `qa/tests/common/CMakeLists.txt`:

```cmake
    test_ai_observability_log.cpp
```

Run:

```powershell
cmd.exe /d /s /c "call ""C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"" >nul && cmake --build out/build/x64-release --target qa_common -- -j 2"
```

Expected: build fails because `kisurf/ai/ai_observability_log.h` does not exist.

- [x] **Step 3: Add the public formatter contract**

Create `include/kisurf/ai/ai_observability_log.h`:

```cpp
#pragma once

#include <kicommon.h>
#include <kisurf/ai/ai_types.h>

#include <cstddef>
#include <vector>
#include <wx/string.h>

enum class AI_AGENT_OBSERVABILITY_KIND
{
    UserInput,
    ModelInput,
    ModelToolCall,
    ToolResult,
    ModelOutput,
    Suggestion,
    System
};

struct KICOMMON_API AI_AGENT_OBSERVABILITY_ENTRY
{
    uint64_t                    m_Sequence = 0;
    uint64_t                    m_RequestId = 0;
    wxString                    m_ToolCallId;
    AI_AGENT_OBSERVABILITY_KIND m_Kind = AI_AGENT_OBSERVABILITY_KIND::System;
    AI_EDITOR_KIND              m_EditorKind = AI_EDITOR_KIND::Unknown;
    wxString                    m_Title;
    wxString                    m_Summary;
    wxString                    m_DetailsJson;
    bool                        m_Allowed = false;
    bool                        m_Executed = false;
    wxString                    m_ErrorCode;
};

class KICOMMON_API AI_AGENT_OBSERVABILITY_LOG
{
public:
    std::vector<AI_AGENT_OBSERVABILITY_ENTRY> Build(
            const std::vector<AI_TRACE_RECORD>& aTraces,
            const std::vector<AI_ACTIVITY_RECORD>& aActivity,
            const std::vector<AI_SUGGESTION_RECORD>& aSuggestions,
            size_t aLimit = 128 ) const;
};
```

- [x] **Step 4: Add minimal implementation**

Create `common/kisurf/ai/ai_observability_log.cpp`:

```cpp
#include <kisurf/ai/ai_observability_log.h>

#include <json_common.h>

#include <algorithm>
#include <string>

namespace
{
std::string toUtf8String( const wxString& aText )
{
    wxScopedCharBuffer buffer = aText.ToUTF8();
    return buffer.data() ? std::string( buffer.data(), buffer.length() ) : std::string();
}

wxString fromUtf8String( const std::string& aText )
{
    return wxString::FromUTF8( aText.c_str() );
}

nlohmann::json visualSummaryJson( const AI_VISUAL_SNAPSHOT& aVisual )
{
    return nlohmann::json{
        { "source", toUtf8String( aVisual.m_Source ) },
        { "mime_type", toUtf8String( aVisual.m_MimeType ) },
        { "width_px", aVisual.m_WidthPx },
        { "height_px", aVisual.m_HeightPx },
        { "byte_size", aVisual.m_ByteSize },
        { "has_pixels", aVisual.HasPixels() }
    };
}

nlohmann::json contextSummaryJson( const AI_CONTEXT_SNAPSHOT& aSnapshot )
{
    return nlohmann::json{
        { "editor", toUtf8String( aSnapshot.AsJsonText( 0, 0, 0 ) ) },
        { "summary", toUtf8String( aSnapshot.m_Summary ) },
        { "selected_count", aSnapshot.m_SelectedObjects.size() },
        { "visible_count", aSnapshot.m_VisibleObjects.size() },
        { "action_count", aSnapshot.m_Actions.size() },
        { "visual", visualSummaryJson( aSnapshot.m_Visual ) }
    };
}

wxString dumpJson( const nlohmann::json& aJson )
{
    return fromUtf8String( aJson.dump() );
}

void appendTraceEntries( const AI_TRACE_RECORD& aTrace,
                         std::vector<AI_AGENT_OBSERVABILITY_ENTRY>& aEntries )
{
    AI_AGENT_OBSERVABILITY_ENTRY input;
    input.m_Sequence = aEntries.size() + 1;
    input.m_RequestId = aTrace.m_RequestId;
    input.m_Kind = AI_AGENT_OBSERVABILITY_KIND::ModelInput;
    input.m_EditorKind = aTrace.m_Request.m_EditorKind;
    input.m_Title = wxS( "Model input" );
    input.m_Summary = aTrace.m_Request.m_UserText;
    input.m_DetailsJson = dumpJson( contextSummaryJson( aTrace.m_Request.m_ContextSnapshot ) );
    aEntries.push_back( std::move( input ) );

    AI_AGENT_OBSERVABILITY_ENTRY output;
    output.m_Sequence = aEntries.size() + 1;
    output.m_RequestId = aTrace.m_RequestId;
    output.m_Kind = AI_AGENT_OBSERVABILITY_KIND::ModelOutput;
    output.m_EditorKind = aTrace.m_Response.m_EditorKind;
    output.m_Title = aTrace.m_Response.m_Title.IsEmpty() ? wxS( "Model output" )
                                                         : aTrace.m_Response.m_Title;
    output.m_Summary = aTrace.m_Response.m_Body;
    aEntries.push_back( std::move( output ) );
}
} // namespace

std::vector<AI_AGENT_OBSERVABILITY_ENTRY> AI_AGENT_OBSERVABILITY_LOG::Build(
        const std::vector<AI_TRACE_RECORD>& aTraces,
        const std::vector<AI_ACTIVITY_RECORD>&,
        const std::vector<AI_SUGGESTION_RECORD>&,
        size_t aLimit ) const
{
    std::vector<AI_AGENT_OBSERVABILITY_ENTRY> entries;

    for( const AI_TRACE_RECORD& trace : aTraces )
        appendTraceEntries( trace, entries );

    if( aLimit > 0 && entries.size() > aLimit )
        entries.erase( entries.begin(), entries.end() - static_cast<std::ptrdiff_t>( aLimit ) );

    for( size_t ii = 0; ii < entries.size(); ++ii )
        entries[ii].m_Sequence = ii + 1;

    return entries;
}
```

- [x] **Step 5: Register the implementation and run green**

Modify `common/CMakeLists.txt` inside `KICOMMON_SRCS`:

```cmake
    kisurf/ai/ai_observability_log.cpp
```

Run:

```powershell
cmd.exe /d /s /c "call ""C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"" >nul && cmake --build out/build/x64-release --target qa_common -- -j 2"
$root = Resolve-Path out\build\x64-release
$env:PATH = "$root\common;$root\common\gal;$root\api;$env:PATH"
& "$root\qa\tests\common\qa_common.exe" --run_test=AiAgentObservabilityLog --log_level=test_suite
```

Expected: `AiAgentObservabilityLog` passes.

- [x] **Step 6: Commit**

```bash
git add include/kisurf/ai/ai_observability_log.h common/kisurf/ai/ai_observability_log.cpp common/CMakeLists.txt qa/tests/common/CMakeLists.txt qa/tests/common/test_ai_observability_log.cpp
git commit -m "feat: add ai agent observability formatter"
```

## Task 2: Tool Activity, Suggestions, Bounding, And Redaction

**Files:**
- Modify: `common/kisurf/ai/ai_observability_log.cpp`
- Modify: `qa/tests/common/test_ai_observability_log.cpp`

- [x] **Step 1: Add failing tool activity merge test**

Append this test:

```cpp
BOOST_AUTO_TEST_CASE( ActivityAddsToolCallAndResultEntries )
{
    AI_ACTIVITY_RECORD toolCall;
    toolCall.m_Sequence = 7;
    toolCall.m_RequestId = 42;
    toolCall.m_ToolCallId = wxS( "call_1" );
    toolCall.m_Kind = AI_ACTIVITY_KIND::ModelToolRequest;
    toolCall.m_EditorKind = AI_EDITOR_KIND::Pcb;
    toolCall.m_ActionName = wxS( "kisurf_run_action" );
    toolCall.m_ArgumentsJson = wxS( "{\"action\":\"common.Control.zoomFitScreen\"}" );

    AI_ACTIVITY_RECORD toolResult;
    toolResult.m_Sequence = 8;
    toolResult.m_RequestId = 42;
    toolResult.m_ToolCallId = wxS( "call_1" );
    toolResult.m_Kind = AI_ACTIVITY_KIND::ToolResult;
    toolResult.m_EditorKind = AI_EDITOR_KIND::Pcb;
    toolResult.m_ActionName = wxS( "common.Control.zoomFitScreen" );
    toolResult.m_ResultJson = wxS( "{\"status\":\"executed\"}" );
    toolResult.m_Allowed = true;
    toolResult.m_Executed = true;

    AI_AGENT_OBSERVABILITY_LOG log;
    std::vector<AI_AGENT_OBSERVABILITY_ENTRY> entries =
            log.Build( {}, { toolCall, toolResult }, {}, 16 );

    BOOST_REQUIRE_EQUAL( entries.size(), 2 );
    BOOST_CHECK( entries.at( 0 ).m_Kind == AI_AGENT_OBSERVABILITY_KIND::ModelToolCall );
    BOOST_CHECK_EQUAL( entries.at( 0 ).m_ToolCallId, wxString( wxS( "call_1" ) ) );
    BOOST_CHECK( entries.at( 0 ).m_DetailsJson.Contains( wxS( "zoomFitScreen" ) ) );
    BOOST_CHECK( entries.at( 1 ).m_Kind == AI_AGENT_OBSERVABILITY_KIND::ToolResult );
    BOOST_CHECK( entries.at( 1 ).m_Allowed );
    BOOST_CHECK( entries.at( 1 ).m_Executed );
}
```

- [x] **Step 2: Add failing suggestion and bounding tests**

Append:

```cpp
BOOST_AUTO_TEST_CASE( SuggestionsProduceLifecycleEntries )
{
    AI_SUGGESTION_RECORD suggestion;
    suggestion.m_Id = 3;
    suggestion.m_Sequence = 11;
    suggestion.m_Title = wxS( "Preview next via" );
    suggestion.m_Body = wxS( "Detected equal via spacing." );
    suggestion.m_Status = AI_SUGGESTION_STATUS::Previewing;
    suggestion.m_ArgumentsJson = wxS( "{\"operation\":\"place_via_preview\"}" );

    AI_AGENT_OBSERVABILITY_LOG log;
    std::vector<AI_AGENT_OBSERVABILITY_ENTRY> entries =
            log.Build( {}, {}, { suggestion }, 16 );

    BOOST_REQUIRE_EQUAL( entries.size(), 1 );
    BOOST_CHECK( entries.front().m_Kind == AI_AGENT_OBSERVABILITY_KIND::Suggestion );
    BOOST_CHECK( entries.front().m_Summary.Contains( wxS( "Previewing" ) ) );
    BOOST_CHECK( entries.front().m_DetailsJson.Contains( wxS( "place_via_preview" ) ) );
}

BOOST_AUTO_TEST_CASE( BuildReturnsMostRecentEntriesWithinLimit )
{
    std::vector<AI_ACTIVITY_RECORD> activity;

    for( int ii = 0; ii < 6; ++ii )
    {
        AI_ACTIVITY_RECORD record;
        record.m_Sequence = static_cast<uint64_t>( ii + 1 );
        record.m_Kind = AI_ACTIVITY_KIND::UserAction;
        record.m_ActionName = wxString::Format( wxS( "action.%d" ), ii + 1 );
        activity.push_back( record );
    }

    AI_AGENT_OBSERVABILITY_LOG log;
    std::vector<AI_AGENT_OBSERVABILITY_ENTRY> entries = log.Build( {}, activity, {}, 3 );

    BOOST_REQUIRE_EQUAL( entries.size(), 3 );
    BOOST_CHECK( entries.at( 0 ).m_Summary.Contains( wxS( "action.4" ) ) );
    BOOST_CHECK( entries.at( 2 ).m_Summary.Contains( wxS( "action.6" ) ) );
}
```

- [x] **Step 3: Add failing redaction test**

Append:

```cpp
BOOST_AUTO_TEST_CASE( DetailsRedactSecretsAndDoNotCopyRawVisualData )
{
    AI_TRACE_RECORD trace;
    trace.m_RequestId = 9;
    trace.m_Request.m_UserText = wxS( "inspect" );
    trace.m_Request.m_ContextSnapshot.m_Visual.m_Source = wxS( "canvas" );
    trace.m_Request.m_ContextSnapshot.m_Visual.m_MimeType = wxS( "image/png" );
    trace.m_Request.m_ContextSnapshot.m_Visual.m_DataUri =
            wxS( "data:image/png;base64,AAAASECRETIMAGEPAYLOAD" );
    trace.m_Request.m_ContextSnapshot.m_Visual.m_WidthPx = 100;
    trace.m_Request.m_ContextSnapshot.m_Visual.m_HeightPx = 50;
    trace.m_Request.m_ContextSnapshot.m_Visual.m_ByteSize = 24;
    trace.m_Request.m_ContextSnapshot.m_Summary =
            wxS( "provider credential: [test-token-value]" );
    trace.m_Response.m_Body = wxS( "ok" );

    AI_AGENT_OBSERVABILITY_LOG log;
    std::vector<AI_AGENT_OBSERVABILITY_ENTRY> entries = log.Build( { trace }, {}, {}, 16 );

    BOOST_REQUIRE_GE( entries.size(), 1 );
    BOOST_CHECK( !entries.front().m_DetailsJson.Contains( wxS( "[test-token-value]" ) ) );
    BOOST_CHECK( !entries.front().m_DetailsJson.Contains( wxS( "AAAASECRETIMAGEPAYLOAD" ) ) );
    BOOST_CHECK( entries.front().m_DetailsJson.Contains( wxS( "\"has_pixels\":true" ) ) );
}
```

- [x] **Step 4: Run red**

Run:

```powershell
$root = Resolve-Path out\build\x64-release
$env:PATH = "$root\common;$root\common\gal;$root\api;$env:PATH"
& "$root\qa\tests\common\qa_common.exe" --run_test=AiAgentObservabilityLog --log_level=test_suite
```

Expected: new tests fail because tool activity, suggestions, bounding, and redaction are not implemented.

- [x] **Step 5: Implement activity and suggestion entries**

Add helper functions to `ai_observability_log.cpp`:

```cpp
wxString suggestionStatusLabel( AI_SUGGESTION_STATUS aStatus );
wxString activitySummary( const AI_ACTIVITY_RECORD& aRecord );
nlohmann::json activityDetailsJson( const AI_ACTIVITY_RECORD& aRecord );
nlohmann::json suggestionDetailsJson( const AI_SUGGESTION_RECORD& aSuggestion );
void appendActivityEntry( const AI_ACTIVITY_RECORD& aRecord,
                          std::vector<AI_AGENT_OBSERVABILITY_ENTRY>& aEntries );
void appendSuggestionEntry( const AI_SUGGESTION_RECORD& aSuggestion,
                            std::vector<AI_AGENT_OBSERVABILITY_ENTRY>& aEntries );
```

Implementation rules:

```cpp
entry.m_Kind = aRecord.m_Kind == AI_ACTIVITY_KIND::ModelToolRequest
             ? AI_AGENT_OBSERVABILITY_KIND::ModelToolCall
             : aRecord.m_Kind == AI_ACTIVITY_KIND::ToolResult
               ? AI_AGENT_OBSERVABILITY_KIND::ToolResult
               : AI_AGENT_OBSERVABILITY_KIND::UserInput;
entry.m_Sequence = aRecord.m_Sequence;
entry.m_RequestId = aRecord.m_RequestId;
entry.m_ToolCallId = aRecord.m_ToolCallId;
entry.m_Title = activityTitle( aRecord );
entry.m_Summary = activitySummary( aRecord );
entry.m_DetailsJson = dumpJson( activityDetailsJson( aRecord ) );
entry.m_Allowed = aRecord.m_Allowed;
entry.m_Executed = aRecord.m_Executed;
entry.m_ErrorCode = aRecord.m_ErrorCode;
```

For suggestions:

```cpp
entry.m_Sequence = aSuggestion.m_Sequence;
entry.m_RequestId = 0;
entry.m_Kind = AI_AGENT_OBSERVABILITY_KIND::Suggestion;
entry.m_EditorKind = aSuggestion.m_EditorKind;
entry.m_Title = wxS( "Suggestion" );
entry.m_Summary = suggestionStatusLabel( aSuggestion.m_Status )
                  + wxS( ": " ) + aSuggestion.m_Title;
entry.m_DetailsJson = dumpJson( suggestionDetailsJson( aSuggestion ) );
```

- [x] **Step 6: Implement redaction**

Add a bounded string sanitizer:

```cpp
wxString redactSensitiveText( const wxString& aText )
{
    wxString text = aText;
    wxRegEx keyPattern( wxS( "sk-[A-Za-z0-9_-]{12,}" ) );
    keyPattern.ReplaceAll( &text, wxS( "sk-[redacted]" ) );

    wxRegEx envPattern( wxS( "(OPENAI_API_KEY|KISURF_AI_API_KEY)=[^\\s\\\"']+" ) );
    envPattern.ReplaceAll( &text, wxS( "\\1=[redacted]" ) );

    if( text.length() > 4000 )
        text = text.Left( 4000 ) + wxS( "...[truncated]" );

    return text;
}
```

Use this sanitizer before writing any user text, summaries, arguments, results,
or context summaries into `m_DetailsJson`.

- [x] **Step 7: Sort, bound, and renumber**

At the end of `Build(...)`, sort by `(sequence == 0 ? request-derived order : sequence)`, then keep the most recent `aLimit` entries. Renumber display `m_Sequence` from 1 to `entries.size()` after bounding.

- [x] **Step 8: Run green**

Run:

```powershell
cmd.exe /d /s /c "call ""C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"" >nul && cmake --build out/build/x64-release --target qa_common -- -j 2"
$root = Resolve-Path out\build\x64-release
$env:PATH = "$root\common;$root\common\gal;$root\api;$env:PATH"
& "$root\qa\tests\common\qa_common.exe" --run_test=AiAgentObservabilityLog --log_level=test_suite
```

Expected: all observability formatter tests pass.

- [x] **Step 9: Commit**

```bash
git add common/kisurf/ai/ai_observability_log.cpp qa/tests/common/test_ai_observability_log.cpp
git commit -m "test: cover ai observability activity entries"
```

## Task 3: Panel Model Observability API

**Files:**
- Modify: `include/kisurf/ai/ai_agent_panel_model.h`
- Modify: `common/kisurf/ai/ai_agent_panel_model.cpp`
- Modify: `qa/tests/common/test_ai_agent_panel_model.cpp`

- [x] **Step 1: Write failing model API test**

Append to `test_ai_agent_panel_model.cpp`:

```cpp
BOOST_AUTO_TEST_CASE( ObservabilityEntriesExposeRuntimeTraceAndActivity )
{
    AI_AGENT_PANEL_MODEL model( std::make_unique<TOOL_CALL_AI_PROVIDER>(), nullptr );
    FAKE_PANEL_TOOL_CALL_HANDLER handler;
    model.SetToolCallHandler( &handler );

    model.SendUserText( wxS( "show agent" ), AI_EDITOR_KIND::Pcb );

    std::vector<AI_AGENT_OBSERVABILITY_ENTRY> entries =
            model.ObservabilityEntries( 16 );

    BOOST_REQUIRE_GE( entries.size(), 4 );

    bool sawInput = false;
    bool sawToolCall = false;
    bool sawToolResult = false;
    bool sawOutput = false;

    for( const AI_AGENT_OBSERVABILITY_ENTRY& entry : entries )
    {
        sawInput |= entry.m_Kind == AI_AGENT_OBSERVABILITY_KIND::ModelInput;
        sawToolCall |= entry.m_Kind == AI_AGENT_OBSERVABILITY_KIND::ModelToolCall;
        sawToolResult |= entry.m_Kind == AI_AGENT_OBSERVABILITY_KIND::ToolResult;
        sawOutput |= entry.m_Kind == AI_AGENT_OBSERVABILITY_KIND::ModelOutput;
    }

    BOOST_CHECK( sawInput );
    BOOST_CHECK( sawToolCall );
    BOOST_CHECK( sawToolResult );
    BOOST_CHECK( sawOutput );
}
```

- [x] **Step 2: Run red**

Run:

```powershell
$root = Resolve-Path out\build\x64-release
$env:PATH = "$root\common;$root\common\gal;$root\api;$env:PATH"
& "$root\qa\tests\common\qa_common.exe" --run_test=AiAgentPanelModel/ObservabilityEntriesExposeRuntimeTraceAndActivity --log_level=test_suite
```

Expected: compile fails because `ObservabilityEntries` and the new entry types are not visible in the model header.

- [x] **Step 3: Add model API**

Modify `include/kisurf/ai/ai_agent_panel_model.h`:

```cpp
#include <kisurf/ai/ai_observability_log.h>
```

Add public method:

```cpp
std::vector<AI_AGENT_OBSERVABILITY_ENTRY> ObservabilityEntries(
        size_t aLimit = 128 ) const;
```

- [x] **Step 4: Implement model API**

Add to `common/kisurf/ai/ai_agent_panel_model.cpp`:

```cpp
std::vector<AI_AGENT_OBSERVABILITY_ENTRY> AI_AGENT_PANEL_MODEL::ObservabilityEntries(
        size_t aLimit ) const
{
    AI_AGENT_OBSERVABILITY_LOG formatter;
    return formatter.Build( m_Runtime.TraceRecords(), ActivityRecords(), Suggestions(), aLimit );
}
```

- [x] **Step 5: Run green**

Run:

```powershell
cmd.exe /d /s /c "call ""C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"" >nul && cmake --build out/build/x64-release --target qa_common -- -j 2"
$root = Resolve-Path out\build\x64-release
$env:PATH = "$root\common;$root\common\gal;$root\api;$env:PATH"
& "$root\qa\tests\common\qa_common.exe" --run_test=AiAgentPanelModel/ObservabilityEntriesExposeRuntimeTraceAndActivity,AiAgentObservabilityLog --log_level=test_suite
```

Expected: targeted tests pass.

- [x] **Step 6: Commit**

```bash
git add include/kisurf/ai/ai_agent_panel_model.h common/kisurf/ai/ai_agent_panel_model.cpp qa/tests/common/test_ai_agent_panel_model.cpp
git commit -m "feat: expose ai observability entries from panel model"
```

## Task 4: Agent Pane Log View

**Files:**
- Modify: `include/kisurf/ai/ai_agent_panel.h`
- Modify: `common/kisurf/ai/ai_agent_panel.cpp`
- Modify: `qa/tests/common/test_ai_agent_panel.cpp`

- [x] **Step 1: Write failing panel surface tests**

Append to `test_ai_agent_panel.cpp`:

```cpp
BOOST_AUTO_TEST_CASE( AgentPanelExposesLogRefreshSurface )
{
    BOOST_CHECK( ( std::is_member_function_pointer_v<decltype( &AI_AGENT_PANEL::RefreshLog )> ) );
}

BOOST_AUTO_TEST_CASE( AgentPanelFormatsObservabilityEntryText )
{
    AI_AGENT_OBSERVABILITY_ENTRY entry;
    entry.m_Sequence = 4;
    entry.m_Kind = AI_AGENT_OBSERVABILITY_KIND::ModelToolCall;
    entry.m_Title = wxS( "Tool call" );
    entry.m_Summary = wxS( "kisurf_run_action" );
    entry.m_ToolCallId = wxS( "call_7" );
    entry.m_Allowed = true;
    entry.m_Executed = false;

    wxString text = AiAgentObservabilityEntryText( entry );

    BOOST_CHECK( text.Contains( wxS( "#4" ) ) );
    BOOST_CHECK( text.Contains( wxS( "Tool call" ) ) );
    BOOST_CHECK( text.Contains( wxS( "call_7" ) ) );
    BOOST_CHECK( text.Contains( wxS( "allowed" ) ) );
}
```

- [x] **Step 2: Run red**

Run:

```powershell
$root = Resolve-Path out\build\x64-release
$env:PATH = "$root\common;$root\common\gal;$root\api;$env:PATH"
& "$root\qa\tests\common\qa_common.exe" --run_test=AiAgentPanel/AgentPanelExposesLogRefreshSurface,AiAgentPanel/AgentPanelFormatsObservabilityEntryText --log_level=test_suite
```

Expected: compile fails because `RefreshLog` and `AiAgentObservabilityEntryText` do not exist.

- [x] **Step 3: Extend panel header**

Modify `include/kisurf/ai/ai_agent_panel.h`:

```cpp
class wxNotebook;
```

Add public method:

```cpp
void RefreshLog();
```

Add private fields:

```cpp
wxNotebook* m_Notebook = nullptr;
wxTextCtrl* m_Log = nullptr;
```

Add exported helper:

```cpp
KICOMMON_API wxString AiAgentObservabilityEntryText(
        const AI_AGENT_OBSERVABILITY_ENTRY& aEntry );
```

- [x] **Step 4: Implement log text helper**

Add to `common/kisurf/ai/ai_agent_panel.cpp`:

```cpp
wxString observabilityKindText( AI_AGENT_OBSERVABILITY_KIND aKind )
{
    switch( aKind )
    {
    case AI_AGENT_OBSERVABILITY_KIND::UserInput: return wxS( "User" );
    case AI_AGENT_OBSERVABILITY_KIND::ModelInput: return wxS( "Input" );
    case AI_AGENT_OBSERVABILITY_KIND::ModelToolCall: return wxS( "Tool" );
    case AI_AGENT_OBSERVABILITY_KIND::ToolResult: return wxS( "Result" );
    case AI_AGENT_OBSERVABILITY_KIND::ModelOutput: return wxS( "Output" );
    case AI_AGENT_OBSERVABILITY_KIND::Suggestion: return wxS( "Suggestion" );
    case AI_AGENT_OBSERVABILITY_KIND::System: return wxS( "System" );
    }

    return wxS( "System" );
}

wxString AiAgentObservabilityEntryText( const AI_AGENT_OBSERVABILITY_ENTRY& aEntry )
{
    wxString text;
    text << wxS( "#" ) << aEntry.m_Sequence << wxS( " " )
         << observabilityKindText( aEntry.m_Kind ) << wxS( ": " )
         << aEntry.m_Title;

    if( !aEntry.m_ToolCallId.IsEmpty() )
        text << wxS( " (" ) << aEntry.m_ToolCallId << wxS( ")" );

    if( aEntry.m_Allowed || aEntry.m_Executed )
    {
        text << wxS( " [" )
             << ( aEntry.m_Allowed ? wxS( "allowed" ) : wxS( "denied" ) )
             << wxS( "/" )
             << ( aEntry.m_Executed ? wxS( "executed" ) : wxS( "not executed" ) )
             << wxS( "]" );
    }

    if( !aEntry.m_Summary.IsEmpty() )
        text << wxS( "\n" ) << aEntry.m_Summary;

    if( !aEntry.m_ErrorCode.IsEmpty() )
        text << wxS( "\nerror: " ) << aEntry.m_ErrorCode;

    if( !aEntry.m_DetailsJson.IsEmpty() )
        text << wxS( "\n" ) << aEntry.m_DetailsJson;

    return text;
}
```

- [x] **Step 5: Add wxNotebook Log tab**

Modify constructor layout:

```cpp
#include <wx/notebook.h>
```

Create notebook pages:

```cpp
m_Notebook = new wxNotebook( this, wxID_ANY );
wxPanel* chatPage = new wxPanel( m_Notebook, wxID_ANY );
wxPanel* previewPage = new wxPanel( m_Notebook, wxID_ANY );
wxPanel* logPage = new wxPanel( m_Notebook, wxID_ANY );
```

Move `m_Transcript` to `chatPage`, `m_Suggestions` and suggestion buttons to
`previewPage`, and `m_Log` to `logPage`.

Use this log control:

```cpp
m_Log = new wxTextCtrl( logPage, wxID_ANY, wxEmptyString, wxDefaultPosition,
                        wxDefaultSize, wxTE_MULTILINE | wxTE_READONLY | wxBORDER_NONE );
```

Add pages:

```cpp
m_Notebook->AddPage( chatPage, _( "Chat" ) );
m_Notebook->AddPage( previewPage, _( "Preview" ) );
m_Notebook->AddPage( logPage, _( "Log" ) );
```

Keep input and Send/Stop buttons below the notebook.

- [x] **Step 6: Implement `RefreshLog`**

Add:

```cpp
void AI_AGENT_PANEL::RefreshLog()
{
    if( !m_Log )
        return;

    wxString text;

    for( const AI_AGENT_OBSERVABILITY_ENTRY& entry : m_Model->ObservabilityEntries( 128 ) )
    {
        text << AiAgentObservabilityEntryText( entry ) << wxS( "\n\n" );
    }

    m_Log->SetValue( text );
    m_Log->SetInsertionPointEnd();
}
```

Call `RefreshLog()` after:

- `SendCurrentText()`
- `PreviewLatestSuggestion()`
- `AcceptLatestSuggestion()`
- `RejectLatestSuggestion()`
- `RecordActivity()`

- [x] **Step 7: Run green**

Run:

```powershell
cmd.exe /d /s /c "call ""C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"" >nul && cmake --build out/build/x64-release --target qa_common -- -j 2"
$root = Resolve-Path out\build\x64-release
$env:PATH = "$root\common;$root\common\gal;$root\api;$env:PATH"
& "$root\qa\tests\common\qa_common.exe" --run_test=AiAgentPanel,AiAgentPanelModel,AiAgentObservabilityLog --log_level=nothing
```

Expected: exit code 0.

- [x] **Step 8: Commit**

```bash
git add include/kisurf/ai/ai_agent_panel.h common/kisurf/ai/ai_agent_panel.cpp qa/tests/common/test_ai_agent_panel.cpp
git commit -m "feat: show ai agent observability log"
```

## Task 5: Final Verification And GUI Smoke

**Files:**
- Modify: `docs/superpowers/plans/2026-06-18-ai-agent-observability-log-implementation.md`

- [x] **Step 1: Run full target build**

Run:

```powershell
cmd.exe /d /s /c "call ""C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"" >nul && cmake --build out/build/x64-release --target qa_common -- -j 2 && cmake --build out/build/x64-release --target pcbnew -- -j 2 && cmake --build out/build/x64-release --target eeschema -- -j 2"
```

Expected: exit code 0.

- [x] **Step 2: Run AI regression tests**

Run:

```powershell
$root = Resolve-Path out\build\x64-release
$env:PATH = "$root\common;$root\common\gal;$root\api;$root\pcbnew;$root\eeschema;$env:PATH"
& "$root\qa\tests\common\qa_common.exe" --run_test=AiNativeProvider,AiActionToolCallHandler,AiToolExecution,AiSuggestionOperations,AiSemanticToolCallHandler,AiAgentPanel,AiAgentPanelModel,AiSuggestionOrchestrator,AiNextActionProvider,AiAgentObservabilityLog --log_level=nothing
```

Expected: exit code 0.

- [ ] **Step 3: Run GUI smoke**

Launch PCB Editor from the build directory with an isolated config:

```powershell
$root = Resolve-Path out\build\x64-release
$env:PATH = "$root\common;$root\common\gal;$root\api;$root\pcbnew;$env:PATH"
$env:KICAD_RUN_FROM_BUILD_DIR = "1"
$env:KICAD_CONFIG_HOME = "$env:TEMP\kisurf-agent-log-smoke-config"
Start-Process "$root\pcbnew\pcbnew.exe" -WorkingDirectory "$root\pcbnew"
```

Smoke steps:

1. Open `AI > Agent`.
2. Confirm the Agent pane shows `Chat`, `Preview`, and `Log` tabs.
3. Send a short prompt such as `show agent status`.
4. Open `Log`.
5. Confirm the log includes model input and model output rows.
6. Trigger a previewable suggestion if a board fixture is open; confirm a
   suggestion row appears.

Status: partially blocked. `pcbnew.exe` launched successfully with an isolated
temporary config copied from the user's 10.99 profile, and the main window title
was `PCB Editor`. Computer Use app approval timed out twice, so the interactive
confirmation of `AI > Agent` and the `Chat` / `Preview` / `Log` tabs could not
be completed in this run.

- [x] **Step 4: Secret and whitespace checks**

Run:

```powershell
git diff --check
git grep -n -E "sk-[0-9A-Za-z_-]{20,}|OPENAI_API_KEY\s*=|KISURF_AI_API_KEY\s*=" -- common include qa docs
```

Expected: whitespace check exit code 0; secret scan has no matches.

- [x] **Step 5: Update this plan status**

Check off completed tasks in this plan as each task is implemented and verified.

- [x] **Step 6: Commit final plan status**

```bash
git add docs/superpowers/plans/2026-06-18-ai-agent-observability-log-implementation.md
git commit -m "docs: update ai observability log plan status"
```

## Self-Review

- Spec coverage: Tasks cover derived formatter, bounded/redacted details, runtime trace and activity merge, suggestion lifecycle entries, panel model API, pane UI, tests, and GUI smoke.
- Placeholder scan: The plan contains concrete file paths, test names, command lines, expected failures, expected passes, and commit messages.
- Type consistency: `AI_AGENT_OBSERVABILITY_ENTRY`, `AI_AGENT_OBSERVABILITY_KIND`, `AI_AGENT_OBSERVABILITY_LOG::Build`, `AI_AGENT_PANEL_MODEL::ObservabilityEntries`, `AI_AGENT_PANEL::RefreshLog`, and `AiAgentObservabilityEntryText` are consistently named across tasks.
- Scope check: Persistent logs, IPC streaming, and a richer card UI are outside this implementation slice.
