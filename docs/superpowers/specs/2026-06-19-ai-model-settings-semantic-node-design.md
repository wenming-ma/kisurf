# AI Model Settings Semantic Node Design

## Purpose

KiSurf now has a real `AI -> Model Settings...` menu item and an Agent-panel
`Model...` button, but the Agent panel's semantic UI tree does not expose that
button to the model. This leaves a mismatch between what the engineer sees and
what the model can inspect through `kisurf_get_workspace_view` or act on through
`kisurf_invoke_semantic_ui_action`.

This slice exposes the model-settings entry as a first-class semantic UI node.
It keeps configuration storage, provider selection, and secret handling
unchanged.

## Requirements

1. Add a stable semantic node id `agent.model.settings`.
2. The node must be a button under `agent.root`.
3. The node label must be `Model Settings`.
4. The node must be enabled and have action `invoke`.
5. The node must not carry text values or any API key material.
6. The semantic panel state JSON returned through workspace/context tools must
   include the node.
7. `AI_AGENT_PANEL::InvokeSemanticUiAction()` must handle
   `agent.model.settings` with action `invoke` by opening the existing local
   model-settings dialog.
8. Opening the settings dialog is considered a local UI action only. This slice
   does not add semantic access to dialog fields and does not allow the model to
   read or write stored API keys.

## Non-Goals

- No Anthropic runtime implementation.
- No model-settings dialog redesign.
- No new secret-storage behavior.
- No automatic entry of API keys.
- No remote/network calls.

## Safety

The semantic node only reveals that a settings button exists. It does not expose
field values. The existing dialog still masks API keys and uses the local model
configuration store. If the model invokes the node, the user sees the same modal
dialog a human would get from the panel button.

## Testing

1. Add a red semantic-tree test proving `agent.model.settings` is expected in
   the stable Agent node list.
2. Add a red semantic-tree test proving the node is an enabled button with
   action `invoke`, label `Model Settings`, no confirmation requirement, and no
   text policy payload.
3. Add a red panel-state JSON test proving the serialized Agent panel state
   includes `agent.model.settings`.
4. Implement the node and action handler.
5. Run targeted `AiAgentPanelSemantic` tests.
6. Run the broader `Ai*` common test slice.
7. Build `pcbnew` and use Computer Use to verify the real Agent panel still
   opens and the `Model...` control is visible without new popup errors.

## Self-Review

- Placeholder scan: no TBD/TODO markers remain.
- Scope check: focused on semantic exposure of an existing settings entry.
- Safety check: no secret values are added to semantic state.
- Ambiguity check: invocation opens the existing local dialog only; dialog
  fields are intentionally not modeled in this slice.
