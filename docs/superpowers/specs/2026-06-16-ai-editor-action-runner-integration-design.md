# AI Editor Action Runner Integration Design

Date: 2026-06-16

## Purpose

This spec connects the common AI tool-call handler to live KiCad editors. The
previous slice can translate model `kisurf_run_action` calls into policy-gated
`AI_ACTION_RUNNER` requests, but the PCB and schematic Agent panes do not yet
install a runner backed by `TOOL_MANAGER`.

The integration remains intentionally narrow. Model output may execute only
explicitly allowlisted read-only actions, with the initial allowlist limited to
`common.Control.showAgentPanel`. The full action catalog is still exposed in
context so the model can inspect available editor capabilities, but execution is
deny-by-default.

## Source Anchors

- `include/kisurf/ai/ai_action_tool_call_handler.h` defines the common handler
  that requires an `AI_ACTION_RUNNER`.
- `include/kisurf/ai/ai_tool_execution.h` defines the deny-by-default execution
  policy and runner interface.
- `include/tool/tool_manager.h` exposes `TOOL_MANAGER::RunAction` by action
  name and `GetActionManager()`.
- `common/tool/tool_manager.cpp` asserts on unknown action names unless callers
  check `ACTION_MANAGER::FindAction(...)` first.
- `common/kisurf/ai/ai_agent_panel.cpp` owns the panel model and sends current
  context snapshots to the runtime.
- `pcbnew/pcb_edit_frame.cpp` and `eeschema/sch_edit_frame.cpp` create the Agent
  pane, context provider, visual snapshot capture, action catalog, and activity
  observer.

## Goals

- Add a reusable `AI_CALLBACK_ACTION_RUNNER` that implements
  `AI_ACTION_RUNNER` over a small callback.
- Validate action names before calling `TOOL_MANAGER::RunAction(...)` so missing
  actions fail as runner errors instead of debug assertions in editor code.
- Add an `AI_AGENT_PANEL` configuration method that owns the execution policy,
  runner, activity log, and `AI_ACTION_TOOL_CALL_HANDLER` for the pane lifetime.
- Install the configured handler into both PCB and schematic Agent panes.
- Keep fallback action descriptors aligned with the editor action catalog used
  in model context.
- Start with a conservative allowlist containing only
  `common.Control.showAgentPanel`.
- Preserve the existing preview/materialization boundary: modifying and
  destructive actions still cannot execute through model tool calls.

## Non-Goals

- No model-driven execution of placement, routing, deletion, save, import,
  update-from-schematic, or other modifying/destructive actions.
- No JSON argument binding into KiCad action parameters.
- No second provider round trip that feeds tool results back to the model.
- No persistent storage for tool execution logs.
- No IPC or external process runner in this slice.

## Design

### Callback Runner

Add a common-layer runner:

```cpp
class KICOMMON_API AI_CALLBACK_ACTION_RUNNER : public AI_ACTION_RUNNER
{
public:
    using ACTION_CALLBACK = std::function<bool( const wxString& aActionName,
                                                wxString& aError )>;

    explicit AI_CALLBACK_ACTION_RUNNER( ACTION_CALLBACK aCallback );

    bool RunActionByName( const wxString& aActionName, wxString& aError ) override;
};
```

`RunActionByName(...)` must fail cleanly if no callback is installed and
otherwise delegate to the callback. The common AI module must not call
`TOOL_MANAGER` directly because `TOOL_MANAGER` and `ACTION_MANAGER` live in the
`common` static target, not the exported `kicommon` AI module.

Editor-provided callbacks must:

1. Fail with a user-readable error if the tool manager is null.
2. Fail if the action name is empty or cannot be converted to UTF-8.
3. Fail if `GetActionManager()` is null.
4. Fail if `FindAction(...)` does not find the action.
5. Call `TOOL_MANAGER::RunAction(std::string)` only after the preflight checks.
6. Return false with an error if `RunAction(...)` reports that no tool handled
   the action.

The callback runner does not classify safety. Safety belongs to
`AI_TOOL_EXECUTION_POLICY`.

### Agent Panel Tool Configuration

Add an `AI_AGENT_PANEL` method:

```cpp
void ConfigureActionToolCalls(
        std::unique_ptr<AI_ACTION_RUNNER> aRunner,
        const std::vector<wxString>& aAllowlistedActions,
        std::vector<AI_ACTION_DESCRIPTOR> aFallbackActions = {} );
```

The panel owns:

- `AI_TOOL_EXECUTION_POLICY m_ToolExecutionPolicy`
- `AI_ACTIVITY_LOG m_ToolActivityLog`
- `std::unique_ptr<AI_ACTION_RUNNER> m_ActionRunner`
- `std::unique_ptr<AI_ACTION_TOOL_CALL_HANDLER> m_ToolCallHandler`

The handler must be destroyed before the runner and policy. The member order
must reflect that lifetime rule.

Calling `ConfigureActionToolCalls(...)` replaces the previous runner/handler,
loads the allowlist into the policy, assigns fallback action descriptors, and
calls `m_Model->SetToolCallHandler(m_ToolCallHandler.get())`. Passing a null
runner clears the installed handler.

### Editor Installation

Both editor frames install the handler immediately after creating the Agent
panel and before user activity can trigger model work.

For PCB:

```cpp
TOOL_MANAGER* toolManager = GetToolManager();

m_agentPanel->ConfigureActionToolCalls(
        std::make_unique<AI_CALLBACK_ACTION_RUNNER>(
                [toolManager]( const wxString& aActionName, wxString& aError )
                {
                    return runAiToolManagerAction( toolManager, aActionName, aError );
                } ),
        { wxS( "common.Control.showAgentPanel" ) },
        AI_ACTION_CATALOG::Build( toolManager->GetActionManager(), AI_EDITOR_KIND::Pcb, 128 ) );
```

For schematic:

```cpp
TOOL_MANAGER* toolManager = GetToolManager();

m_agentPanel->ConfigureActionToolCalls(
        std::make_unique<AI_CALLBACK_ACTION_RUNNER>(
                [toolManager]( const wxString& aActionName, wxString& aError )
                {
                    return runAiToolManagerAction( toolManager, aActionName, aError );
                } ),
        { wxS( "common.Control.showAgentPanel" ) },
        AI_ACTION_CATALOG::Build( toolManager->GetActionManager(),
                                  AI_EDITOR_KIND::Schematic, 128 ) );
```

If `GetToolManager()` is null, the editor leaves tool-call execution
unconfigured and the runtime records tool calls without executing them.

## Error Handling

- Missing callback: runner returns false and sets
  `Action callback is not available.`
- Null tool manager: editor callback returns false and sets
  `Tool manager is not available.`
- Null action manager: editor callback returns false and sets
  `Action manager is not available.`
- Empty action: editor callback returns false and sets `Action name is empty.`
- Unknown action: editor callback returns false and includes the requested action
  name.
- Unhandled action: editor callback returns false and includes the requested
  action name.
- Policy denials still use existing executor error codes such as
  `not_allowlisted`, `requires_preview`, and `destructive_denied`.

## Testing Requirements

Common tests:

- `AI_CALLBACK_ACTION_RUNNER` fails cleanly with a missing callback.
- `AI_CALLBACK_ACTION_RUNNER` delegates the action name to its callback and
  preserves callback errors.
- `AI_AGENT_PANEL` exposes a tool-call configuration surface.
- `AI_AGENT_PANEL_MODEL` continues to route handler results into response tool
  calls.

Build verification:

- `qa_common` targeted suites pass for:
  `AiCallbackActionRunner`, `AiAgentPanel`, `AiActionToolCallHandler`,
  `AiAgentPanelModel`, `AiNativeRuntime`, and `AiToolExecution`.
- `qa_pcbnew` targeted AI suites still build and pass.
- `qa_eeschema` targeted AI suites still build and pass.

## Acceptance Criteria

- PCB and schematic Agent panes install a configured tool-call handler when a
  tool manager is available.
- Model `kisurf_check_action` calls can dry-run against current context action
  descriptors.
- Model `kisurf_run_action` calls can execute only
  `common.Control.showAgentPanel` in this slice.
- Missing actions fail without triggering `TOOL_MANAGER` unknown-action
  assertions because editor callbacks preflight `ACTION_MANAGER::FindAction(...)`.
- No API key, base URL secret, pointer address, or raw editor object pointer is
  written into traces or tests.

## Spec Self-Review

- Open-marker scan: no unresolved markers remain.
- Scope check: this spec only wires live action execution for the existing
  policy-gated handler; preview/edit materialization remains separate.
- Safety check: execution stays deny-by-default and the initial allowlist has a
  single read-only panel action.
- Compatibility check: common runtime remains editor-agnostic, while
  `AI_AGENT_PANEL` owns the live handler lifetime for each editor pane.
