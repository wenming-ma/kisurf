# AI Visual Unavailable Reason Implementation Plan

## Checklist

1. Add failing tests for:
   - invalid image unavailable reason;
   - context JSON/prompt propagation;
   - visual-frame tool propagation.
2. Add `m_UnavailableReason` to `AI_VISUAL_SNAPSHOT`.
3. Populate reasons in image/canvas visual capture code.
4. Surface `unavailable_reason` in structured context JSON, prompt text, visual-frame JSON, workspace-view visual JSON, and observability summaries.
5. Verify:
   - targeted visual tests;
   - focused AI suite;
   - `pcbnew` and `eeschema` builds;
   - `git diff --check`;
   - dynamic secret scan.

## Review Notes

The reason field should describe the capture state, not a user-facing error. It should help the model decide whether to proceed with semantic context, ask for a retry, or avoid making visual claims.
