# Agent Observability Readable Log Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make the Agent panel Log tab show readable model-input and model-output debugging details from existing structured observability entries.

**Architecture:** Keep `AI_AGENT_OBSERVABILITY_LOG` and runtime trace capture unchanged. Add parsing and compact text rendering inside `AiAgentObservabilityEntryText()` so existing `RefreshLog()` automatically displays richer diagnostics.

**Tech Stack:** C++20, wxWidgets `wxString`, `nlohmann::json`, Boost unit tests, existing KiSurf Agent observability types.

---

## File Structure

- Modify: `common/kisurf/ai/ai_agent_panel.cpp`
  - Add safe JSON parsing helpers near existing panel-formatting helpers.
  - Extend `AiAgentObservabilityEntryText()` with model input/output detail lines.
- Modify: `qa/tests/common/test_ai_agent_panel.cpp`
  - Add red tests for readable `ModelInput` and `ModelOutput` details.
- Create: `docs/superpowers/specs/2026-06-19-agent-observability-readable-log-design.md`
- Create: `docs/superpowers/plans/2026-06-19-agent-observability-readable-log-implementation.md`

## Task 1: Red Tests

**Files:**
- Modify: `qa/tests/common/test_ai_agent_panel.cpp`

- [ ] **Step 1: Add the JSON include**

Add:

```cpp
#include <json_common.h>
```

near the other includes so tests can build details JSON with the same
dependency used by production Agent code.

- [ ] **Step 2: Add model input readable-details test**

Add this test in the `AiAgentPanel` suite:

```cpp
BOOST_AUTO_TEST_CASE( AgentPanelFormatsModelInputObservabilityDetails )
{
    AI_AGENT_OBSERVABILITY_ENTRY entry;
    entry.m_Sequence = 5;
    entry.m_RequestId = 42;
    entry.m_Kind = AI_AGENT_OBSERVABILITY_KIND::ModelInput;
    entry.m_Title = wxS( "Model input" );
    entry.m_Summary = wxS( "route selected net" );
    entry.m_DetailsJson = wxString::FromUTF8(
            nlohmann::json{
                    { "request_id", 42 },
                    { "editor", "pcb" },
                    { "tool_results_count", 1 },
                    { "context",
                      {
                              { "selected_count", 2 },
                              { "visible_count", 5 },
                              { "anchor_count", 3 },
                              { "panel_state_count", 1 },
                              { "tool_state_kind", "routing_track" },
                              { "visual",
                                {
                                        { "source", "pcbnew.canvas" },
                                        { "width_px", 1280 },
                                        { "height_px", 720 },
                                        { "has_pixels", true }
                                } }
                      } }
            }.dump().c_str() );

    wxString text = AiAgentObservabilityEntryText( entry );

    BOOST_CHECK( text.Contains( wxS( "details:" ) ) );
    BOOST_CHECK( text.Contains( wxS( "request=42" ) ) );
    BOOST_CHECK( text.Contains( wxS( "editor=pcb" ) ) );
    BOOST_CHECK( text.Contains( wxS( "selected=2" ) ) );
    BOOST_CHECK( text.Contains( wxS( "visible=5" ) ) );
    BOOST_CHECK( text.Contains( wxS( "anchors=3" ) ) );
    BOOST_CHECK( text.Contains( wxS( "panels=1" ) ) );
    BOOST_CHECK( text.Contains( wxS( "tool_state=routing_track" ) ) );
    BOOST_CHECK( text.Contains( wxS( "visual=pcbnew.canvas 1280x720" ) ) );
    BOOST_CHECK( text.Contains( wxS( "tool_results=1" ) ) );
}
```

- [ ] **Step 3: Add model output readable-details test**

Add:

```cpp
BOOST_AUTO_TEST_CASE( AgentPanelFormatsModelOutputObservabilityDetails )
{
    AI_AGENT_OBSERVABILITY_ENTRY entry;
    entry.m_Sequence = 6;
    entry.m_RequestId = 42;
    entry.m_Kind = AI_AGENT_OBSERVABILITY_KIND::ModelOutput;
    entry.m_Title = wxS( "Routing assistant" );
    entry.m_Summary = wxS( "I can preview the next segment." );
    entry.m_DetailsJson = wxString::FromUTF8(
            nlohmann::json{
                    { "request_id", 42 },
                    { "body_length", 31 },
                    { "tool_call_count", 2 },
                    { "cancelled", false }
            }.dump().c_str() );

    wxString text = AiAgentObservabilityEntryText( entry );

    BOOST_CHECK( text.Contains( wxS( "details:" ) ) );
    BOOST_CHECK( text.Contains( wxS( "request=42" ) ) );
    BOOST_CHECK( text.Contains( wxS( "body_length=31" ) ) );
    BOOST_CHECK( text.Contains( wxS( "tool_calls=2" ) ) );
    BOOST_CHECK( text.Contains( wxS( "cancelled=false" ) ) );
}
```

- [ ] **Step 4: Verify RED**

Run:

```powershell
cmd /c "call ""C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"" >nul && cmake --build .\out\build\x64-release --target qa_common --config Release"
```

Expected: build succeeds, but `AiAgentPanel` targeted tests fail because the
new expected `details:` text is not emitted yet.

Then run:

```powershell
$dllDirs = Get-ChildItem .\out\build\x64-release -Recurse -Filter *.dll | Select-Object -ExpandProperty DirectoryName -Unique
$env:PATH = ($dllDirs -join ';') + ';' + $env:PATH
.\out\build\x64-release\qa\tests\common\qa_common.exe --run_test=AiAgentPanel --report_level=short
```

Expected: the two new tests fail on missing details fields.

## Task 2: Implement Readable Details

**Files:**
- Modify: `common/kisurf/ai/ai_agent_panel.cpp`

- [ ] **Step 1: Include JSON support**

Add:

```cpp
#include <json_common.h>
```

above the local includes or directly after the KiSurf AI includes.

- [ ] **Step 2: Add parse helpers**

Add helpers in the anonymous namespace:

```cpp
std::string toUtf8String( const wxString& aText )
{
    wxScopedCharBuffer buffer = aText.ToUTF8();
    return buffer.data() ? std::string( buffer.data(), buffer.length() ) : std::string();
}

wxString fromUtf8String( const std::string& aText )
{
    return wxString::FromUTF8( aText.c_str() );
}

std::optional<nlohmann::json> parseEntryDetails( const wxString& aDetailsJson )
{
    if( aDetailsJson.IsEmpty() )
        return std::nullopt;

    nlohmann::json details =
            nlohmann::json::parse( toUtf8String( aDetailsJson ), nullptr, false );

    if( details.is_discarded() || !details.is_object() )
        return std::nullopt;

    return details;
}
```

If `toUtf8String()` or `fromUtf8String()` already exists in this file, use the
existing helper instead of adding a duplicate.

- [ ] **Step 3: Add safe field readers**

Add:

```cpp
wxString jsonStringField( const nlohmann::json& aJson, const char* aName )
{
    auto it = aJson.find( aName );
    return it != aJson.end() && it->is_string()
           ? fromUtf8String( it->get<std::string>() )
           : wxString();
}

wxString jsonIntFieldText( const nlohmann::json& aJson, const char* aName )
{
    auto it = aJson.find( aName );
    return it != aJson.end() && it->is_number_integer()
           ? wxString::Format( wxS( "%lld" ), it->get<long long>() )
           : wxString();
}

wxString jsonBoolFieldText( const nlohmann::json& aJson, const char* aName )
{
    auto it = aJson.find( aName );
    return it != aJson.end() && it->is_boolean()
           ? ( it->get<bool>() ? wxString( wxS( "true" ) )
                               : wxString( wxS( "false" ) ) )
           : wxString();
}
```

- [ ] **Step 4: Add model input detail renderer**

Add:

```cpp
wxString modelInputDetailLine( const nlohmann::json& aDetails )
{
    wxString line;
    line << wxS( "details:" );
    appendDetailField( line, wxS( "request" ),
                       jsonIntFieldText( aDetails, "request_id" ) );
    appendDetailField( line, wxS( "editor" ),
                       jsonStringField( aDetails, "editor" ) );

    auto contextIt = aDetails.find( "context" );

    if( contextIt != aDetails.end() && contextIt->is_object() )
    {
        appendDetailField( line, wxS( "selected" ),
                           jsonIntFieldText( *contextIt, "selected_count" ) );
        appendDetailField( line, wxS( "visible" ),
                           jsonIntFieldText( *contextIt, "visible_count" ) );
        appendDetailField( line, wxS( "anchors" ),
                           jsonIntFieldText( *contextIt, "anchor_count" ) );
        appendDetailField( line, wxS( "panels" ),
                           jsonIntFieldText( *contextIt, "panel_state_count" ) );
        appendDetailField( line, wxS( "tool_state" ),
                           jsonStringField( *contextIt, "tool_state_kind" ) );

        auto visualIt = contextIt->find( "visual" );

        if( visualIt != contextIt->end() && visualIt->is_object() )
        {
            wxString visual = jsonStringField( *visualIt, "source" );
            wxString width = jsonIntFieldText( *visualIt, "width_px" );
            wxString height = jsonIntFieldText( *visualIt, "height_px" );

            if( !visual.IsEmpty() && !width.IsEmpty() && !height.IsEmpty() )
                visual << wxS( " " ) << width << wxS( "x" ) << height;

            appendDetailField( line, wxS( "visual" ), visual );
        }
    }

    appendDetailField( line, wxS( "tool_results" ),
                       jsonIntFieldText( aDetails, "tool_results_count" ) );
    return line == wxS( "details:" ) ? wxString() : line;
}
```

Define `appendDetailField()` before this renderer:

```cpp
void appendDetailField( wxString& aLine, const wxString& aName,
                        const wxString& aValue )
{
    if( aValue.IsEmpty() )
        return;

    aLine << wxS( " " ) << aName << wxS( "=" ) << aValue;
}
```

- [ ] **Step 5: Add model output detail renderer**

Add:

```cpp
wxString modelOutputDetailLine( const nlohmann::json& aDetails )
{
    wxString line;
    line << wxS( "details:" );
    appendDetailField( line, wxS( "request" ),
                       jsonIntFieldText( aDetails, "request_id" ) );
    appendDetailField( line, wxS( "body_length" ),
                       jsonIntFieldText( aDetails, "body_length" ) );
    appendDetailField( line, wxS( "tool_calls" ),
                       jsonIntFieldText( aDetails, "tool_call_count" ) );
    appendDetailField( line, wxS( "cancelled" ),
                       jsonBoolFieldText( aDetails, "cancelled" ) );
    return line == wxS( "details:" ) ? wxString() : line;
}
```

- [ ] **Step 6: Wire into `AiAgentObservabilityEntryText()`**

In `AiAgentObservabilityEntryText()`, after existing summary/error/details
logic, parse `aEntry.m_DetailsJson` and append model input/output detail lines:

```cpp
std::optional<nlohmann::json> details = parseEntryDetails( aEntry.m_DetailsJson );

if( details )
{
    wxString line;

    if( aEntry.m_Kind == AI_AGENT_OBSERVABILITY_KIND::ModelInput )
        line = modelInputDetailLine( *details );
    else if( aEntry.m_Kind == AI_AGENT_OBSERVABILITY_KIND::ModelOutput )
        line = modelOutputDetailLine( *details );

    if( !line.IsEmpty() )
        text << wxS( "\n" ) << line;
}
```

Do not dump raw `m_DetailsJson` into the log.

## Task 3: Verification

**Files:**
- All touched files.

- [ ] **Step 1: Build `qa_common`**

Run:

```powershell
cmd /c "call ""C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"" >nul && cmake --build .\out\build\x64-release --target qa_common --config Release"
```

Expected: exit 0.

- [ ] **Step 2: Run targeted tests**

Run:

```powershell
$dllDirs = Get-ChildItem .\out\build\x64-release -Recurse -Filter *.dll | Select-Object -ExpandProperty DirectoryName -Unique
$env:PATH = ($dllDirs -join ';') + ';' + $env:PATH
.\out\build\x64-release\qa\tests\common\qa_common.exe --run_test=AiAgentPanel --report_level=short
```

Expected: exit 0.

- [ ] **Step 3: Run broad Agent tests**

Run:

```powershell
$dllDirs = Get-ChildItem .\out\build\x64-release -Recurse -Filter *.dll | Select-Object -ExpandProperty DirectoryName -Unique
$env:PATH = ($dllDirs -join ';') + ';' + $env:PATH
.\out\build\x64-release\qa\tests\common\qa_common.exe --run_test=Ai* --report_level=short
```

Expected: exit 0.

- [ ] **Step 4: Build PCB Editor**

Run:

```powershell
cmd /c "call ""C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"" >nul && cmake --build .\out\build\x64-release --target pcbnew --config Release"
```

Expected: exit 0.

- [ ] **Step 5: Attempt GUI smoke**

Run:

```powershell
.\tools\run_from_build.ps1 -BuildDir .\out\build\x64-release -App pcbnew
```

Use Computer Use to check for missing-DLL/runtime popups and open `AI -> Agent`.
If Computer Use reports `app approval timed out`, record that exact blocker and
do not claim click verification.

- [ ] **Step 6: Static and secret checks**

Run:

```powershell
git diff --check -- common/kisurf/ai/ai_agent_panel.cpp qa/tests/common/test_ai_agent_panel.cpp docs/superpowers/specs/2026-06-19-agent-observability-readable-log-design.md docs/superpowers/plans/2026-06-19-agent-observability-readable-log-implementation.md
$prefix = 'sk' + '-'
$pattern = $prefix + '[A-Za-z0-9_-]{20,}|OPENAI_API_KEY=.*' + $prefix
rg -n $pattern common/kisurf/ai/ai_agent_panel.cpp qa/tests/common/test_ai_agent_panel.cpp docs/superpowers/specs/2026-06-19-agent-observability-readable-log-design.md docs/superpowers/plans/2026-06-19-agent-observability-readable-log-implementation.md
```

Expected: no whitespace errors and no secret matches.

- [ ] **Step 7: Commit**

Run:

```powershell
git add common/kisurf/ai/ai_agent_panel.cpp qa/tests/common/test_ai_agent_panel.cpp docs/superpowers/specs/2026-06-19-agent-observability-readable-log-design.md docs/superpowers/plans/2026-06-19-agent-observability-readable-log-implementation.md
git commit -m "ui: show readable agent observability details"
```

Do not stage unrelated `qa/tests/pcbnew/test_module.cpp`.

## Self-Review

- Spec coverage: all readable model input/output requirements map to tests and
  formatter implementation.
- Placeholder scan: no undefined tasks or deferred implementation notes remain.
- Type consistency: helper names and public function names match existing
  `AiAgentObservabilityEntryText()` and `AI_AGENT_OBSERVABILITY_ENTRY` types.
