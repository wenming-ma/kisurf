# AI Background Agent Toggle Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan one task at a time. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add an explicit background Agent switch that gates automatic activity-driven suggestions while preserving activity recording and context-mode updates.

**Architecture:** Put the authoritative enabled state and suggestion gate in `AI_AGENT_PANEL_MODEL` so the behavior is unit-testable. Expose matching methods and a checkbox in `AI_AGENT_PANEL`, then have `RecordActivity()` call the gated model method instead of unconditional `UpdateSuggestions()`.

**Tech Stack:** C++20, wxWidgets controls, Boost unit tests, existing `qa_common` target.

---

## File Structure

- Modify `include/kisurf/ai/ai_agent_panel_model.h`
  - Add background Agent enabled accessors.
  - Add gated `UpdateSuggestionsIfBackgroundEnabled(...)`.
- Modify `common/kisurf/ai/ai_agent_panel_model.cpp`
  - Store `m_BackgroundAgentEnabled`, default false.
  - Implement the gated suggestion method.
- Modify `include/kisurf/ai/ai_agent_panel.h`
  - Add panel-level accessors.
  - Add `wxCheckBox* m_BackgroundAgentToggle`.
- Modify `common/kisurf/ai/ai_agent_panel.cpp`
  - Add checkbox to UI.
  - Bind it to the model state.
  - Use `UpdateSuggestionsIfBackgroundEnabled()` in `RecordActivity()`.
- Modify `qa/tests/common/test_ai_agent_panel_model.cpp`
  - Add model tests for default disabled state, disabled provider suppression, and enabled delegation.
- Modify `qa/tests/common/test_ai_agent_panel.cpp`
  - Add type-surface tests for panel background toggle accessors.
- Update this plan with completion status and verification evidence.

## Task 1: Red Tests

**Files:**
- Modify: `qa/tests/common/test_ai_agent_panel_model.cpp`
- Modify: `qa/tests/common/test_ai_agent_panel.cpp`

- [x] **Step 1: Add model toggle tests**

Add tests:

```cpp
BOOST_AUTO_TEST_CASE( BackgroundAgentIsDisabledByDefault )
{
    AI_AGENT_PANEL_MODEL model( std::make_unique<AI_STUB_PROVIDER>(), nullptr );

    BOOST_CHECK( !model.BackgroundAgentEnabled() );
}

BOOST_AUTO_TEST_CASE( BackgroundAgentDisabledSuppressesAutomaticSuggestions )
{
    auto* suggestionProvider = new FAKE_SUGGESTION_PROVIDER();
    suggestionProvider->m_NextSuggestion = makeModelSuggestion();

    AI_AGENT_PANEL_MODEL model(
            std::make_unique<AI_STUB_PROVIDER>(),
            std::unique_ptr<AI_SUGGESTION_PROVIDER>( suggestionProvider ) );

    std::optional<AI_SUGGESTION_RECORD> suggestion =
            model.UpdateSuggestionsIfBackgroundEnabled(
                    makeSuggestionContext(), makeSuggestionActivity(), wxS( "activity" ) );

    BOOST_CHECK( !suggestion.has_value() );
    BOOST_CHECK_EQUAL( suggestionProvider->m_CallCount, 0 );
}

BOOST_AUTO_TEST_CASE( BackgroundAgentEnabledDelegatesToSuggestionProvider )
{
    auto* suggestionProvider = new FAKE_SUGGESTION_PROVIDER();
    suggestionProvider->m_NextSuggestion = makeModelSuggestion();

    AI_AGENT_PANEL_MODEL model(
            std::make_unique<AI_STUB_PROVIDER>(),
            std::unique_ptr<AI_SUGGESTION_PROVIDER>( suggestionProvider ) );
    model.SetBackgroundAgentEnabled( true );

    std::optional<AI_SUGGESTION_RECORD> suggestion =
            model.UpdateSuggestionsIfBackgroundEnabled(
                    makeSuggestionContext(), makeSuggestionActivity(), wxS( "activity" ) );

    BOOST_REQUIRE( suggestion.has_value() );
    BOOST_CHECK_EQUAL( suggestionProvider->m_CallCount, 1 );
}
```

- [x] **Step 2: Add panel API surface test**

Add to `test_ai_agent_panel.cpp`:

```cpp
BOOST_AUTO_TEST_CASE( AgentPanelExposesBackgroundAgentToggleSurface )
{
    BOOST_CHECK( ( std::is_member_function_pointer_v<
            decltype( &AI_AGENT_PANEL::SetBackgroundAgentEnabled )> ) );
    BOOST_CHECK( ( std::is_member_function_pointer_v<
            decltype( &AI_AGENT_PANEL::BackgroundAgentEnabled )> ) );
}
```

- [x] **Step 3: Verify RED**

Run:

```powershell
cmd.exe /S /C '"C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 && cmake --build out\build\x64-release --target qa_common --config Release'
```

Expected: compilation fails because the new methods do not exist.

Actual: red build failed as expected because `AI_AGENT_PANEL_MODEL` did not yet expose
`BackgroundAgentEnabled`, `UpdateSuggestionsIfBackgroundEnabled`, or
`SetBackgroundAgentEnabled`, and `AI_AGENT_PANEL` did not yet expose
`SetBackgroundAgentEnabled` or `BackgroundAgentEnabled`.

## Task 2: Implement Model Gate

**Files:**
- Modify: `include/kisurf/ai/ai_agent_panel_model.h`
- Modify: `common/kisurf/ai/ai_agent_panel_model.cpp`

- [x] **Step 1: Add model state and API**

Add public methods:

```cpp
void SetBackgroundAgentEnabled( bool aEnabled );
bool BackgroundAgentEnabled() const { return m_BackgroundAgentEnabled; }
std::optional<AI_SUGGESTION_RECORD> UpdateSuggestionsIfBackgroundEnabled(
        AI_CONTEXT_SNAPSHOT aContextSnapshot, AI_ACTIVITY_RECORD aActivity,
        const wxString& aReason );
```

Add private member:

```cpp
bool m_BackgroundAgentEnabled = false;
```

- [x] **Step 2: Implement methods**

```cpp
void AI_AGENT_PANEL_MODEL::SetBackgroundAgentEnabled( bool aEnabled )
{
    m_BackgroundAgentEnabled = aEnabled;
}

std::optional<AI_SUGGESTION_RECORD>
AI_AGENT_PANEL_MODEL::UpdateSuggestionsIfBackgroundEnabled(
        AI_CONTEXT_SNAPSHOT aContextSnapshot, AI_ACTIVITY_RECORD aActivity,
        const wxString& aReason )
{
    if( !m_BackgroundAgentEnabled )
        return std::nullopt;

    return UpdateSuggestions( std::move( aContextSnapshot ), std::move( aActivity ),
                              aReason );
}
```

## Task 3: Implement Panel UI And RecordActivity Gate

**Files:**
- Modify: `include/kisurf/ai/ai_agent_panel.h`
- Modify: `common/kisurf/ai/ai_agent_panel.cpp`

- [x] **Step 1: Add checkbox member and methods**

Add `wxCheckBox` include/forward declaration, member pointer, and public methods:

```cpp
void SetBackgroundAgentEnabled( bool aEnabled );
bool BackgroundAgentEnabled() const;
```

- [x] **Step 2: Add UI checkbox**

Create:

```cpp
m_BackgroundAgentToggle = new wxCheckBox( this, wxID_ANY, _( "Background Agent" ) );
m_BackgroundAgentToggle->SetValue( m_Model->BackgroundAgentEnabled() );
```

Add it to the control row near mode controls.

Bind:

```cpp
m_BackgroundAgentToggle->Bind( wxEVT_CHECKBOX, [this]( wxCommandEvent& )
{
    SetBackgroundAgentEnabled( m_BackgroundAgentToggle->GetValue() );
} );
```

- [x] **Step 3: Gate automatic suggestions**

Change `RecordActivity()` to call:

```cpp
m_Model->UpdateSuggestionsIfBackgroundEnabled( std::move( snapshot ),
                                               std::move( activity ),
                                               wxS( "activity" ) );
```

Keep mode update, stale suggestion expiry, and log refresh outside the gate.

## Task 4: Verify And Commit

**Files:**
- Modify: `docs/superpowers/plans/2026-06-19-ai-background-agent-toggle-implementation.md`

- [x] **Step 1: Build and targeted tests**

Run:

```powershell
cmd.exe /S /C '"C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 && cmake --build out\build\x64-release --target qa_common --config Release'
```

Run:

```powershell
.\qa_common.exe --run_test=AiAgentPanelModel
.\qa_common.exe --run_test=AiAgentPanel
```

Expected: both suites exit 0. The known schema-file warning may still appear.

Actual:
- `cmake --build out\build\x64-release --target qa_common --config Release` exited 0.
- `qa_common.exe --run_test=AiAgentPanelModel` exited 0 with the known schema-file warning and `No errors detected`.
- `qa_common.exe --run_test=AiAgentPanel` exited 0 with the known schema-file warning and `No errors detected`.

- [x] **Step 2: Static checks**

Run:

```powershell
git diff --check
rg -n "s[k]-|OPENAI_API_KEY[[:space:]]*=" include\kisurf\ai\ai_agent_panel.h include\kisurf\ai\ai_agent_panel_model.h common\kisurf\ai\ai_agent_panel.cpp common\kisurf\ai\ai_agent_panel_model.cpp qa\tests\common\test_ai_agent_panel.cpp qa\tests\common\test_ai_agent_panel_model.cpp docs\superpowers\specs\2026-06-19-ai-background-agent-toggle-design.md docs\superpowers\plans\2026-06-19-ai-background-agent-toggle-implementation.md
```

Expected: `git diff --check` exits 0; secret scan has no matches.

Actual:
- `git diff --check` exited 0.
- Secret scan exited with no matches.

- [x] **Step 3: Update plan and commit**

Commit only touched files:

```powershell
git add include/kisurf/ai/ai_agent_panel.h include/kisurf/ai/ai_agent_panel_model.h common/kisurf/ai/ai_agent_panel.cpp common/kisurf/ai/ai_agent_panel_model.cpp qa/tests/common/test_ai_agent_panel.cpp qa/tests/common/test_ai_agent_panel_model.cpp docs/superpowers/plans/2026-06-19-ai-background-agent-toggle-implementation.md
git commit -m "feat: gate background ai suggestions"
```

Do not stage unrelated `qa/tests/pcbnew/test_module.cpp`.

## Self-review

- Spec coverage: every requirement maps to model API, panel API, UI checkbox, RecordActivity gate, or tests.
- Placeholder scan: no TBD/TODO/implement-later placeholders remain.
- Type consistency: method names are consistently `SetBackgroundAgentEnabled`, `BackgroundAgentEnabled`, and `UpdateSuggestionsIfBackgroundEnabled`.
