# AI User Activity Input Details Implementation Plan

## Checklist

1. Add failing tests to `AiEditorActivityRecorder` for:
   - left click with coordinates and Shift/Ctrl details;
   - right click primary button detail.
2. Replace manual arguments JSON assembly with structured JSON construction.
3. Add helpers for button and modifier names.
4. Verify:
   - `AiEditorActivityRecorder`;
   - focused activity/workspace AI tests;
   - `qa_common` build;
   - editor builds;
   - `git diff --check`;
   - dynamic secret scan.

## Notes

This slice improves model observability without increasing event volume. Drag/motion preview can be added later with throttling and context-aware sampling.
