# AI Semantic UI Confirmation Guard Implementation Plan

## Checklist

1. Add tests for:
   - Agent semantic tree serializes confirmation requirements;
   - Agent panel semantic accept invoke denies missing confirmation;
   - confirmed accept still delegates to the existing review handler.
2. Add confirmation fields to semantic UI node/request structs.
3. Serialize the node field in Agent panel state JSON.
4. Mark `agent.accept` as requiring user confirmation.
5. Guard `AI_AGENT_PANEL::InvokeSemanticUiAction()` for `agent.accept`.
6. Verify:
   - Agent panel semantic tests;
   - Agent panel tests;
   - focused AI suite;
   - editor builds;
   - `git diff --check`;
   - dynamic secret scan.

## Notes

This keeps the model-facing surface read-only for now while making the future invoke bridge safer to add.
