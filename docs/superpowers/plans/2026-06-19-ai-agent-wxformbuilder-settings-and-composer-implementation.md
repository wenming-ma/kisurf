# AI Agent wxFormBuilder Settings and Composer Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Move the model settings dialog to a wxFormBuilder-generated base and polish the Agent chat composer structure.

**Architecture:** Add a generated `AI_MODEL_SETTINGS_DIALOG_BASE` beside the existing Agent panel base. Update the existing Agent panel `.fbp` so the composer is a single generated panel with input, status text, and actions. Keep business logic in `AI_AGENT_PANEL` subclasses and preserve existing model configuration behavior.

**Tech Stack:** C++17, wxWidgets, wxFormBuilder 4.2.1, Boost unit tests, CMake.

---

## File Map

- Create `common/kisurf/ai/ai_model_settings_dialog_base.fbp`: wxFormBuilder source for the model settings dialog.
- Create `common/kisurf/ai/ai_model_settings_dialog_base.h`: generated base dialog declaration.
- Create `common/kisurf/ai/ai_model_settings_dialog_base.cpp`: generated base dialog implementation.
- Modify `common/kisurf/ai/ai_agent_panel_base.fbp`: generated Agent panel source with composer container/status controls.
- Modify `common/kisurf/ai/ai_agent_panel_base.h`: generated Agent panel declaration.
- Modify `common/kisurf/ai/ai_agent_panel_base.cpp`: generated Agent panel implementation.
- Modify `common/kisurf/ai/ai_agent_panel.cpp`: concrete settings dialog now inherits `AI_MODEL_SETTINGS_DIALOG_BASE`.
- Modify `common/CMakeLists.txt`: compile the new generated dialog base `.cpp`.
- Modify `qa/tests/common/test_ai_agent_panel.cpp`: generated surface tests for composer controls.
- Create or modify `qa/tests/common/test_ai_model_settings_dialog.cpp`: generated surface tests for settings dialog controls.
- Modify `qa/tests/common/CMakeLists.txt`: register the new dialog test if a new file is used.

## Task 1: Red Tests for Generated Surfaces

- [ ] **Step 1: Add settings dialog surface test**

Create `qa/tests/common/test_ai_model_settings_dialog.cpp` with a protected-test subclass:

```cpp
#include <boost/test/unit_test.hpp>
#include <kisurf/ai/ai_model_settings_dialog_base.h>

#include <type_traits>
#include <wx/dialog.h>

BOOST_AUTO_TEST_SUITE( AiModelSettingsDialog )

class AI_MODEL_SETTINGS_DIALOG_BASE_SURFACE_TEST : public AI_MODEL_SETTINGS_DIALOG_BASE
{
public:
    explicit AI_MODEL_SETTINGS_DIALOG_BASE_SURFACE_TEST( wxWindow* aParent ) :
            AI_MODEL_SETTINGS_DIALOG_BASE( aParent )
    {
    }

    static constexpr bool HasProviderChoice()
    {
        return std::is_member_object_pointer_v<
                decltype( &AI_MODEL_SETTINGS_DIALOG_BASE_SURFACE_TEST::m_ProviderChoice )>;
    }

    static constexpr bool HasBaseUrl()
    {
        return std::is_member_object_pointer_v<
                decltype( &AI_MODEL_SETTINGS_DIALOG_BASE_SURFACE_TEST::m_BaseUrl )>;
    }

    static constexpr bool HasModel()
    {
        return std::is_member_object_pointer_v<
                decltype( &AI_MODEL_SETTINGS_DIALOG_BASE_SURFACE_TEST::m_Model )>;
    }

    static constexpr bool HasApiKey()
    {
        return std::is_member_object_pointer_v<
                decltype( &AI_MODEL_SETTINGS_DIALOG_BASE_SURFACE_TEST::m_ApiKey )>;
    }

    static constexpr bool HasHelpText()
    {
        return std::is_member_object_pointer_v<
                decltype( &AI_MODEL_SETTINGS_DIALOG_BASE_SURFACE_TEST::m_HelpText )>;
    }
};

BOOST_AUTO_TEST_CASE( DialogBaseExposesGeneratedSettingsSurface )
{
    BOOST_CHECK( ( std::is_base_of_v<wxDialog, AI_MODEL_SETTINGS_DIALOG_BASE> ) );
    BOOST_CHECK( AI_MODEL_SETTINGS_DIALOG_BASE_SURFACE_TEST::HasProviderChoice() );
    BOOST_CHECK( AI_MODEL_SETTINGS_DIALOG_BASE_SURFACE_TEST::HasBaseUrl() );
    BOOST_CHECK( AI_MODEL_SETTINGS_DIALOG_BASE_SURFACE_TEST::HasModel() );
    BOOST_CHECK( AI_MODEL_SETTINGS_DIALOG_BASE_SURFACE_TEST::HasApiKey() );
    BOOST_CHECK( AI_MODEL_SETTINGS_DIALOG_BASE_SURFACE_TEST::HasHelpText() );
}

BOOST_AUTO_TEST_SUITE_END()
```

- [ ] **Step 2: Add composer surface expectations**

Extend `qa/tests/common/test_ai_agent_panel.cpp` so the protected surface test also checks `m_ComposerPanel` and `m_ComposerStatus`.

- [ ] **Step 3: Verify red**

Run:

```powershell
cmake --build out\build\x64-release --target qa_common --config Release
```

Expected: build fails because `ai_model_settings_dialog_base.h`, `m_ComposerPanel`, or `m_ComposerStatus` does not exist yet.

## Task 2: Generate wxFormBuilder UI

- [ ] **Step 1: Create settings dialog `.fbp`**

Create `common/kisurf/ai/ai_model_settings_dialog_base.fbp` with:

- Project file `ai_model_settings_dialog_base`.
- Dialog class `AI_MODEL_SETTINGS_DIALOG_BASE`.
- Protected controls named `m_ProviderChoice`, `m_BaseUrl`, `m_Model`, `m_ApiKey`, `m_HelpText`, and `m_StdDialogButtons`.
- `m_ApiKey` style includes `wxTE_PASSWORD`.
- OK and Cancel buttons use a standard dialog button sizer.

- [ ] **Step 2: Update Agent panel `.fbp`**

Modify `common/kisurf/ai/ai_agent_panel_base.fbp` so the bottom composer has:

- Protected `wxPanel* m_ComposerPanel`.
- Protected `wxStaticText* m_ComposerStatus`.
- Existing `m_Input`, `m_SendButton`, and `m_StopButton` inside that panel.
- The input minimum height remains at least 76 logical pixels.

- [ ] **Step 3: Generate C++ from wxFormBuilder**

Run:

```powershell
& 'C:\Program Files\wxFormBuilder\wxFormBuilder.exe' --generate common\kisurf\ai\ai_agent_panel_base.fbp
& 'C:\Program Files\wxFormBuilder\wxFormBuilder.exe' --generate common\kisurf\ai\ai_model_settings_dialog_base.fbp
```

Expected: generated `.h/.cpp` files are updated or created without hand-editing generated code.

## Task 3: Wire Concrete Dialog

- [ ] **Step 1: Include generated base**

Modify `common/kisurf/ai/ai_agent_panel.cpp` to include:

```cpp
#include <kisurf/ai/ai_model_settings_dialog_base.h>
```

- [ ] **Step 2: Replace hand-built dialog layout**

Change local `AI_MODEL_SETTINGS_DIALOG` to inherit `AI_MODEL_SETTINGS_DIALOG_BASE`. Its constructor should fill provider choices, set field values, bind provider switching, and call `SetMinSize( FromDIP( wxSize( 520, -1 ) ) )`. `Config()` should read from the generated controls.

- [ ] **Step 3: Register generated source**

Add `kisurf/ai/ai_model_settings_dialog_base.cpp` to the common AI source list in `common/CMakeLists.txt`.

- [ ] **Step 4: Verify green**

Run:

```powershell
cmake --build out\build\x64-release --target qa_common --config Release
.\out\build\x64-release\qa\qa_common.exe --run_test=AiAgentPanel,AiModelSettingsDialog,AiModelConfig --report_level=short
```

Expected: targeted tests pass.

## Task 4: Full Verification and GUI Smoke

- [ ] **Step 1: Run AI tests**

Run:

```powershell
.\out\build\x64-release\qa\qa_common.exe --run_test=Ai* --report_level=short
```

Expected: AI suite passes.

- [ ] **Step 2: Build PCB Editor**

Run:

```powershell
cmake --build out\build\x64-release --target pcbnew --config Release
```

Expected: build exits 0.

- [ ] **Step 3: Use Computer Use**

Launch the build-tree PCB Editor and use Computer Use to inspect the window for blocking system popups and, if the helper allows screenshots/clicks, open the Agent panel and Model Settings dialog.

- [ ] **Step 4: Run checks**

Run `git diff --check` on changed files and a dynamic secret scan for `sk-` style API keys. The scan must not print the key.

## Task 5: Commit

- [ ] **Step 1: Stage only this slice**

Stage the two new docs, generated UI files, implementation files, and tests. Do not stage unrelated `qa/tests/pcbnew/test_module.cpp`.

- [ ] **Step 2: Commit**

Commit with:

```powershell
git commit -m "ui: add wxformbuilder model settings dialog"
```

Expected: commit succeeds and the unrelated pcbnew test file remains unstaged.

## Self-Review

- Spec coverage: The plan covers generated dialog UI, generated composer polish, business wiring, tests, wxFormBuilder generation, build, GUI smoke, and secret scan.
- Placeholder scan: No TBD, TODO, or deferred implementation placeholders are present.
- Type consistency: Generated member names used in tests match the member names required in `.fbp` and the concrete dialog wiring.
