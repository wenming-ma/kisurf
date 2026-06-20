# AI OpenAI Multimodal Visual Transport Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Send native canvas visual snapshots to the OpenAI-compatible provider as multimodal `image_url` message parts.

**Architecture:** Keep capture and resizing in `AI_VISUAL_SNAPSHOT`; change only provider request serialization. Text-only requests remain string content, while requests with `m_Visual.HasPixels()` become array content with a text part and an image URL part.

**Tech Stack:** C++17, nlohmann JSON, KiCad common AI provider, Boost.Test.

---

## File Structure

- Modify: `docs/superpowers/specs/2026-06-16-kisurf-ai-native-spec-index.md`
- Modify: `common/kisurf/ai/ai_provider.cpp`
- Modify: `qa/tests/common/test_ai_provider.cpp`

## Verification Commands

Provider targeted verification:

```bat
cmd.exe /d /s /c """C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat"" -arch=x64 -host_arch=x64 >nul && cmake --build out/build/x64-release --target qa_common -- -j 2 && set KICAD_RUN_FROM_BUILD_DIR=1 && set KICAD_BUILD_PATHS=C:/Users/wenming/source/repos/kisurf/out/build/x64-release/kicad:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/common:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/api:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/common/gal:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/pcbnew:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/eeschema:C:/Users/wenming/source/repos/kisurf/out/build/x64-release/cvpcb && set PATH=D:\Tools\vcpkg\installed\x64-windows\bin;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\kicad;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\common;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\api;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\common\gal;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\pcbnew;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\eeschema;C:\Users\wenming\source\repos\kisurf\out\build\x64-release\cvpcb;%PATH% && out\build\x64-release\qa\tests\common\qa_common.exe --run_test=AiNativeProvider,AiAgentPanelModel,AiNativeRuntime --log_level=test_suite
```

The known missing schema warning is acceptable only when Boost reports no errors.

## Task 1: Provider Multimodal Tests

**Files:**
- Modify: `qa/tests/common/test_ai_provider.cpp`

- [ ] **Step 1: Add a failing visual payload test**

Add:

```cpp
BOOST_AUTO_TEST_CASE( OpenAiProviderSendsVisualSnapshotAsImageUrlContent )
{
    AI_PROVIDER_SETTINGS settings;
    settings.m_BaseUrl = wxS( "https://sub2api.wenming-dev.org/v1" );
    settings.m_ApiKey = wxS( "unit-test-key" );
    settings.m_Model = wxS( "unit-model" );

    const wxString dataUri = wxS( "data:image/png;base64,dW5pdA==" );

    AI_OPENAI_COMPAT_PROVIDER provider(
            settings,
            [&]( const AI_HTTP_REQUEST& aRequest, AI_HTTP_RESPONSE& aResponse, wxString& aError )
            {
                wxUnusedVar( aError );

                nlohmann::json body = nlohmann::json::parse( aRequest.m_Body.ToStdString() );
                const nlohmann::json& content = body["messages"].at( 1 )["content"];

                BOOST_REQUIRE( content.is_array() );
                BOOST_REQUIRE_EQUAL( content.size(), 2 );
                BOOST_CHECK_EQUAL( content.at( 0 )["type"].get<std::string>(), "text" );
                BOOST_CHECK( content.at( 0 )["text"].get<std::string>().find(
                                     "visual: test.image image/png pixels=yes" )
                             != std::string::npos );
                BOOST_CHECK_EQUAL( content.at( 1 )["type"].get<std::string>(), "image_url" );
                BOOST_CHECK_EQUAL( content.at( 1 )["image_url"]["url"].get<std::string>(),
                                   dataUri.ToStdString() );

                aResponse.m_StatusCode = 200;
                aResponse.m_Body = wxS( "{\"choices\":[{\"message\":{\"content\":\"saw image\"}}]}" );
                return true;
            } );

    AI_PROVIDER_REQUEST request;
    request.m_RequestId = 31;
    request.m_EditorKind = AI_EDITOR_KIND::Pcb;
    request.m_UserText = wxS( "inspect visible routing" );
    request.m_ContextSnapshot.m_EditorKind = AI_EDITOR_KIND::Pcb;
    request.m_ContextSnapshot.m_Visual.m_Source = wxS( "test.image" );
    request.m_ContextSnapshot.m_Visual.m_MimeType = wxS( "image/png" );
    request.m_ContextSnapshot.m_Visual.m_DataUri = dataUri;
    request.m_ContextSnapshot.m_Visual.m_WidthPx = 4;
    request.m_ContextSnapshot.m_Visual.m_HeightPx = 2;
    request.m_ContextSnapshot.m_Visual.m_ByteSize = 8;

    AI_PROVIDER_RESPONSE response = provider.Generate( request );

    BOOST_CHECK_EQUAL( response.m_RequestId, 31 );
    BOOST_CHECK_EQUAL( response.m_Body, wxString( wxS( "saw image" ) ) );
}
```

- [ ] **Step 2: Add a text-only regression test**

Add:

```cpp
BOOST_AUTO_TEST_CASE( OpenAiProviderKeepsStringContentWithoutVisualPixels )
{
    AI_PROVIDER_SETTINGS settings;
    settings.m_BaseUrl = wxS( "https://sub2api.wenming-dev.org/v1" );
    settings.m_ApiKey = wxS( "unit-test-key" );
    settings.m_Model = wxS( "unit-model" );

    AI_OPENAI_COMPAT_PROVIDER provider(
            settings,
            []( const AI_HTTP_REQUEST& aRequest, AI_HTTP_RESPONSE& aResponse, wxString& aError )
            {
                wxUnusedVar( aError );

                nlohmann::json body = nlohmann::json::parse( aRequest.m_Body.ToStdString() );
                BOOST_REQUIRE( body["messages"].at( 1 )["content"].is_string() );

                aResponse.m_StatusCode = 200;
                aResponse.m_Body = wxS( "{\"choices\":[{\"message\":{\"content\":\"text only\"}}]}" );
                return true;
            } );

    AI_PROVIDER_REQUEST request;
    request.m_RequestId = 32;
    request.m_UserText = wxS( "inspect text context" );

    AI_PROVIDER_RESPONSE response = provider.Generate( request );

    BOOST_CHECK_EQUAL( response.m_RequestId, 32 );
    BOOST_CHECK_EQUAL( response.m_Body, wxString( wxS( "text only" ) ) );
}
```

- [ ] **Step 3: Run tests to verify RED**

Run the provider targeted verification command.

Expected: `OpenAiProviderSendsVisualSnapshotAsImageUrlContent` fails because the
provider still emits a string content field.

## Task 2: Provider Multimodal Serialization

**Files:**
- Modify: `common/kisurf/ai/ai_provider.cpp`

- [ ] **Step 1: Add a user-content helper**

Add to the anonymous namespace:

```cpp
nlohmann::json makeUserMessageContent( const wxString& aUserContent,
                                       const AI_VISUAL_SNAPSHOT& aVisual )
{
    if( !aVisual.HasPixels() )
        return toUtf8String( aUserContent );

    return nlohmann::json::array(
            { { { "type", "text" }, { "text", toUtf8String( aUserContent ) } },
              { { "type", "image_url" },
                { "image_url", { { "url", toUtf8String( aVisual.m_DataUri ) } } } } } );
}
```

- [ ] **Step 2: Use the helper in request construction**

Replace:

```cpp
{ { "role", "user" }, { "content", toUtf8String( userContent ) } }
```

with:

```cpp
{ { "role", "user" },
  { "content",
    makeUserMessageContent( userContent, aRequest.m_ContextSnapshot.m_Visual ) } }
```

- [ ] **Step 3: Run provider verification**

Run the provider targeted verification command.

Expected: provider, panel-model, and runtime target suites pass.

## Task 3: Spec Index, Diff Check, And Commit

**Files:**
- All files above.

- [ ] **Step 1: Update the spec index**

Add:

```markdown
18. [AI OpenAI Multimodal Visual Transport](./2026-06-16-ai-openai-multimodal-visual-transport-design.md)
    - Defines transport of native visual snapshots as OpenAI-compatible multimodal image content.
```

Add implementation order:

```markdown
22. Phase 15 OpenAI-compatible multimodal visual transport that sends native canvas data URIs as image content.
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
git add docs\superpowers\specs\2026-06-16-ai-openai-multimodal-visual-transport-design.md docs\superpowers\specs\2026-06-16-kisurf-ai-native-spec-index.md docs\superpowers\plans\2026-06-16-ai-openai-multimodal-visual-transport-implementation.md common\kisurf\ai\ai_provider.cpp qa\tests\common\test_ai_provider.cpp
git commit -m "feat: send ai visual snapshots to provider"
```

## Plan Self-Review

- Spec coverage: tasks cover visual image transport, text-only compatibility,
  offline tests, spec index updates, and verification.
- Open-marker scan: no unresolved placeholders remain.
- Type consistency: the helper uses existing `AI_VISUAL_SNAPSHOT`,
  `HasPixels()`, and `toUtf8String(...)` symbols already available in
  `ai_provider.cpp`.
