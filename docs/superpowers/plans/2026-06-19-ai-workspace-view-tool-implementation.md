# AI Workspace View Tool Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan one task at a time. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build `kisurf_get_workspace_view`, the preferred single read-only Agent interface for structured context, visual frame, and activity timeline.

**Architecture:** Reuse the existing semantic read-tool parsers and JSON builders in `ai_semantic_tool_call_handler.cpp`, then compose their outputs into a single `workspace_view` payload. Keep the focused tools for compatibility and expose the new unified tool in the OpenAI-compatible provider schema.

**Tech Stack:** C++20, nlohmann JSON, wxString, Boost unit tests, existing `qa_common` target.

---

## File Structure

- Modify `common/kisurf/ai/ai_semantic_tool_call_handler.cpp`
  - Add `kisurf_get_workspace_view` to supported semantic tools.
  - Parse top-level `views`, `context`, `visual`, and `activity` parameters.
  - Reuse `parseContextToolOptions()`, `parseVisualFrameToolOptions()`, `parseActivityTimelineToolOptions()`, `contextSnapshotJson()`, `visualFrameJson()`, and activity serialization helpers.
  - Return `workspace_view_ready` or the existing fail-closed denial for malformed arguments and oversize visual pixels.
- Modify `common/kisurf/ai/ai_provider.cpp`
  - Add JSON schema for the unified workspace view tool.
  - Add `kisurf_get_workspace_view` to the provider tool list.
- Modify `qa/tests/common/test_ai_semantic_tool_call_handler.cpp`
  - Add red tests for default all-section output, selective views, malformed views, nested malformed options, and visual oversize behavior.
- Modify `qa/tests/common/test_ai_provider.cpp`
  - Update tool count and verify the unified schema is advertised.
- Update this plan with completion status and verification evidence.

## Task 1: Red Tests For Workspace View Tool

**Files:**
- Modify: `qa/tests/common/test_ai_semantic_tool_call_handler.cpp`
- Modify: `qa/tests/common/test_ai_provider.cpp`

- [ ] **Step 1: Add semantic handler tests**

Add tests that call `kisurf_get_workspace_view` using existing fixtures:

```cpp
BOOST_AUTO_TEST_CASE( WorkspaceViewToolReturnsAllSectionsByDefault )
{
    AI_SEMANTIC_TOOL_CALL_HANDLER handler( nullptr );

    AI_TOOL_INVOCATION_RESULT result = handler.HandleToolCall(
            requestWithActivityTimeline(),
            toolCall( wxS( "kisurf_get_workspace_view" ), wxS( "{}" ) ) );

    BOOST_CHECK( result.m_Allowed );
    BOOST_CHECK( !result.m_Executed );
    BOOST_CHECK_EQUAL( result.m_ErrorCode, wxString() );

    nlohmann::json payload = nlohmann::json::parse( result.m_ResultJson.ToStdString() );
    BOOST_CHECK_EQUAL( payload["status"].get<std::string>(), "workspace_view_ready" );
    BOOST_CHECK_EQUAL( payload["tool"].get<std::string>(), "kisurf_get_workspace_view" );

    const nlohmann::json& view = payload["workspace_view"];
    BOOST_CHECK( view.contains( "context" ) );
    BOOST_CHECK( view.contains( "visual" ) );
    BOOST_CHECK( view.contains( "activity" ) );
    BOOST_CHECK_EQUAL( view["context"]["editor"].get<std::string>(), "pcb" );
    BOOST_CHECK_EQUAL( view["visual"]["source"].get<std::string>(), "pcbnew.canvas" );
    BOOST_CHECK_EQUAL( view["activity"]["activity_count"].get<int>(), 4 );
}

BOOST_AUTO_TEST_CASE( WorkspaceViewToolReturnsOnlyRequestedViewsWithNestedOptions )
{
    AI_SEMANTIC_TOOL_CALL_HANDLER handler( nullptr );

    AI_TOOL_INVOCATION_RESULT result = handler.HandleToolCall(
            requestWithActivityTimeline(),
            toolCall( wxS( "kisurf_get_workspace_view" ),
                      wxS( "{\"views\":[\"visual\",\"activity\",\"visual\"],"
                           "\"visual\":{\"include_pixels\":true,\"max_bytes\":4096},"
                           "\"activity\":{\"kind\":\"tool_result\"}}" ) ) );

    BOOST_CHECK( result.m_Allowed );

    nlohmann::json payload = nlohmann::json::parse( result.m_ResultJson.ToStdString() );
    const nlohmann::json& view = payload["workspace_view"];
    BOOST_CHECK( !view.contains( "context" ) );
    BOOST_CHECK( view.contains( "visual" ) );
    BOOST_CHECK( view.contains( "activity" ) );
    BOOST_CHECK_EQUAL( view["visual"]["data_uri"].get<std::string>(),
                       "data:image/png;base64,dW5pdA==" );
    BOOST_CHECK_EQUAL( view["activity"]["activity_count"].get<int>(), 1 );
    BOOST_REQUIRE_EQUAL( view["activity"]["records"].size(), 1 );
    BOOST_CHECK_EQUAL( view["activity"]["records"][0]["kind"].get<std::string>(),
                       "tool_result" );
}

BOOST_AUTO_TEST_CASE( WorkspaceViewToolRejectsMalformedArguments )
{
    AI_SEMANTIC_TOOL_CALL_HANDLER handler( nullptr );

    AI_TOOL_INVOCATION_RESULT badView = handler.HandleToolCall(
            requestWithActivityTimeline(),
            toolCall( wxS( "kisurf_get_workspace_view" ),
                      wxS( "{\"views\":[\"layers\"]}" ) ) );

    BOOST_CHECK( !badView.m_Allowed );
    BOOST_CHECK_EQUAL( badView.m_ErrorCode,
                       wxString( wxS( "malformed_arguments" ) ) );

    AI_TOOL_INVOCATION_RESULT badNested = handler.HandleToolCall(
            requestWithActivityTimeline(),
            toolCall( wxS( "kisurf_get_workspace_view" ),
                      wxS( "{\"activity\":{\"kind\":\"mouse\"}}" ) ) );

    BOOST_CHECK( !badNested.m_Allowed );
    BOOST_CHECK_EQUAL( badNested.m_ErrorCode,
                       wxString( wxS( "malformed_arguments" ) ) );
}

BOOST_AUTO_TEST_CASE( WorkspaceViewToolDeniesOversizeVisualPixels )
{
    AI_SEMANTIC_TOOL_CALL_HANDLER handler( nullptr );

    AI_TOOL_INVOCATION_RESULT result = handler.HandleToolCall(
            requestWithActivityTimeline(),
            toolCall( wxS( "kisurf_get_workspace_view" ),
                      wxS( "{\"views\":[\"visual\"],"
                           "\"visual\":{\"include_pixels\":true,\"max_bytes\":1024}}" ) ) );

    BOOST_CHECK( !result.m_Allowed );
    BOOST_CHECK_EQUAL( result.m_ErrorCode, wxString( wxS( "visual_too_large" ) ) );
}
```

- [ ] **Step 2: Add provider schema test expectations**

Update `OpenAiProviderDeclaresKiSurfTools`:

```cpp
BOOST_REQUIRE_EQUAL( body["tools"].size(), 9 );
BOOST_CHECK( std::find( toolNames.begin(), toolNames.end(),
                        "kisurf_get_workspace_view" ) != toolNames.end() );

const nlohmann::json& workspaceParameters =
        toolByName["kisurf_get_workspace_view"]["function"]["parameters"];
BOOST_CHECK( workspaceParameters["required"].empty() );
BOOST_CHECK( !workspaceParameters["additionalProperties"].get<bool>() );
BOOST_CHECK( workspaceParameters["properties"].contains( "views" ) );
BOOST_CHECK( workspaceParameters["properties"].contains( "context" ) );
BOOST_CHECK( workspaceParameters["properties"].contains( "visual" ) );
BOOST_CHECK( workspaceParameters["properties"].contains( "activity" ) );
```

- [ ] **Step 3: Run tests to verify RED**

Run:

```powershell
cmd.exe /S /C '"C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 && cmake --build out\build\x64-release --target qa_common --config Release'
```

Then run:

```powershell
$env:KICAD_RUN_FROM_BUILD_DIR='1'
$env:KICAD_BUILD_PATHS='C:/Users/wenming/source/repos/kisurf/out/build/x64-release/kicad;C:/Users/wenming/source/repos/kisurf/out/build/x64-release/common;C:/Users/wenming/source/repos/kisurf/out/build/x64-release/api;C:/Users/wenming/source/repos/kisurf/out/build/x64-release/common/gal;C:/Users/wenming/source/repos/kisurf/out/build/x64-release/pcbnew;C:/Users/wenming/source/repos/kisurf/out/build/x64-release/eeschema;C:/Users/wenming/source/repos/kisurf/out/build/x64-release/cvpcb'
$env:PATH='D:\Tools\vcpkg\installed\x64-windows\bin;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\kicad;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\common;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\api;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\common\gal;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\pcbnew;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\eeschema;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\cvpcb;'+$env:PATH
.\qa_common.exe --run_test=AiSemanticToolCallHandler
.\qa_common.exe --run_test=AiNativeProvider
```

Expected: semantic handler fails with `unknown_tool`; provider test fails with tool count `8 != 9`.

## Task 2: Implement Semantic Handler

**Files:**
- Modify: `common/kisurf/ai/ai_semantic_tool_call_handler.cpp`

- [ ] **Step 1: Add workspace view support helpers**

Implement helpers that parse top-level args, choose views, and compose existing section JSON:

```cpp
struct WORKSPACE_VIEW_TOOL_OPTIONS
{
    bool                           m_Context = true;
    bool                           m_Visual = true;
    bool                           m_Activity = true;
    CONTEXT_TOOL_OPTIONS           m_ContextOptions;
    VISUAL_FRAME_TOOL_OPTIONS      m_VisualOptions;
    ACTIVITY_TIMELINE_TOOL_OPTIONS m_ActivityOptions;
};
```

Add parser rules:

- top-level fields: `views`, `context`, `visual`, `activity`
- `views` must be an array of strings
- accepted view strings: `context`, `visual`, `activity`
- nested option objects use existing parse helpers

- [ ] **Step 2: Add workspace view result**

Compose:

```cpp
nlohmann::json workspaceView;

if( options.m_Context )
    workspaceView["context"] = contextSnapshotJson( aRequest.m_ContextSnapshot,
                                                    options.m_ContextOptions );

if( options.m_Visual )
    workspaceView["visual"] = visualFrameJson( visual, options.m_VisualOptions.m_IncludePixels );

if( options.m_Activity )
    workspaceView["activity"] = activityTimelineJson( aRequest.m_ContextSnapshot.m_RecentActivity,
                                                      options.m_ActivityOptions );
```

Return `visual_too_large` before serializing pixels when needed.

- [ ] **Step 3: Add dispatch**

Add `kisurf_get_workspace_view` to `supportedTool()` and route it before the suggestion-sink requirement.

## Task 3: Implement Provider Schema

**Files:**
- Modify: `common/kisurf/ai/ai_provider.cpp`

- [ ] **Step 1: Add schema helper**

Add `workspaceViewToolParameters()` with optional top-level `views`, `context`, `visual`, and `activity` properties.

- [ ] **Step 2: Advertise tool**

Add `kisurf_get_workspace_view` to the tools array with a description that marks it as the preferred single read-only workspace interface.

## Task 4: Verify And Commit

**Files:**
- Modify: `docs/superpowers/plans/2026-06-19-ai-workspace-view-tool-implementation.md`

- [ ] **Step 1: Build and targeted tests**

Run:

```powershell
cmd.exe /S /C '"C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 && cmake --build out\build\x64-release --target qa_common --config Release'
```

Run targeted suites with the CTest environment variables:

```powershell
.\qa_common.exe --run_test=AiSemanticToolCallHandler
.\qa_common.exe --run_test=AiNativeProvider
```

Expected: both suites exit 0. The known schema-file warning may still appear.

- [ ] **Step 2: Static checks**

Run:

```powershell
git diff --check
rg -n "s[k]-|OPENAI_API_KEY[[:space:]]*=" common\kisurf\ai\ai_semantic_tool_call_handler.cpp common\kisurf\ai\ai_provider.cpp qa\tests\common\test_ai_semantic_tool_call_handler.cpp qa\tests\common\test_ai_provider.cpp docs\superpowers\specs\2026-06-19-ai-workspace-view-tool-design.md docs\superpowers\plans\2026-06-19-ai-workspace-view-tool-implementation.md
```

Expected: `git diff --check` exits 0; secret scan has no matches.

- [ ] **Step 3: Update plan status and commit**

Add implementation status and verification evidence to this plan, then commit only touched files:

```powershell
git add common/kisurf/ai/ai_provider.cpp common/kisurf/ai/ai_semantic_tool_call_handler.cpp docs/superpowers/plans/2026-06-19-ai-workspace-view-tool-implementation.md qa/tests/common/test_ai_provider.cpp qa/tests/common/test_ai_semantic_tool_call_handler.cpp
git commit -m "feat: add ai workspace view tool"
```

Do not stage `qa/tests/pcbnew/test_module.cpp` unless it is intentionally part of this task.

## Self-review

- Spec coverage: every requirement in the workspace view spec is covered by tasks for parsing, section selection, nested options, visual oversize denial, provider schema, and tests.
- Placeholder scan: no TBD/TODO/implement-later placeholders remain.
- Type consistency: tool name is consistently `kisurf_get_workspace_view`; response status is consistently `workspace_view_ready`; response root is consistently `workspace_view`.

## Implementation Status

Completed on 2026-06-19.

- Added `kisurf_get_workspace_view` provider schema with optional `views`, `context`, `visual`, and `activity` parameters.
- Added read-only semantic handler support that works without a suggestion sink.
- Reused context, visual, and activity parsers/serializers to keep focused tools and the unified tool consistent.
- Added tests for default all-section output, selective/nested views, malformed arguments, visual oversize denial, and provider schema exposure.

Verification:

- `cmake --build out\build\x64-release --target qa_common --config Release`
- `qa_common.exe --run_test=AiSemanticToolCallHandler`
- `qa_common.exe --run_test=AiNativeProvider`
- `git diff --check`
- Secret scan for touched files
