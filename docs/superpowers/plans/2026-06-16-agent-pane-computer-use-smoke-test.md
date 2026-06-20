# Agent Pane Computer Use Smoke Test

Date: 2026-06-16

## Purpose

Verify the first Agent pane with Computer Use before investing in a native semantic-tree automation interface.

## Preconditions

- `pcbnew_kiface`, `eeschema_kiface`, and `kicad` build from `out/build/x64-release`.
- The Agent pane implementation is present.
- The stub provider is active.

## Manual Steps

1. Launch KiCad from `out/build/x64-release`.
2. Open a sample project.
3. Open PCB Editor.
4. Use View > Panels > Agent.
5. Confirm the Agent pane appears.
6. Type `hello from pcb`.
7. Press Send.
8. Confirm the transcript shows the user message and a `Stub Agent` response.
9. Open Schematic Editor.
10. Use View > Panels > Agent.
11. Type `hello from schematic`.
12. Press Send.
13. Confirm the transcript shows the user message and a `Stub Agent` response.

## Computer Use Trial Script

Use Computer Use to perform the same flow:

1. Identify the KiCad main window.
2. Open the PCB editor.
3. Click the View menu.
4. Click the Agent panel item.
5. Click the Agent input.
6. Enter `hello from computer use`.
7. Click Send.
8. Verify visible transcript text contains `Stub Agent`.

## Pass Criteria

- The pane can be shown in PCB and schematic editors.
- The input accepts text.
- Send appends a deterministic response.
- Computer Use can identify and click the relevant UI controls well enough for early smoke testing.

## Escalation Criteria

If Computer Use cannot reliably identify the Agent pane, menus, or Send button after two implementation iterations, create the native semantic-tree automation plan from the validation/self-test spec.

## Trial Result

The earlier Computer Use pass result is no longer sufficient evidence for the
current AI-native stack. A later observability smoke attempt launched
`pcbnew.exe` but could not complete the interactive Agent pane checks because
Computer Use app approval timed out twice. On 2026-06-19, the current Computer
Use helper failed during bootstrap with an internal `@oai/sky` package export
error before KiSurf could be launched through the automation API.

Current status: GUI smoke is unverified. The next planning step is a native
semantic-tree self-test interface for debug/test builds, so KiSurf can verify
Agent pane controls, text entry, send actions, and screenshot metadata without
depending on external desktop automation availability.
