# AI User Activity Input Details Design

## Problem

The activity recorder already captures user commands, selection events, movement events, and mouse clicks. For click events, however, the model only receives coordinates. It cannot distinguish left/right/middle click intent or modifier-assisted interactions such as Shift-click range selection or Ctrl-click additive selection.

## Goals

- Preserve the low-volume activity policy: record clicks, double-clicks, selection, movement, and commands; continue ignoring mouse motion and drag spam.
- Add stable mouse input details to `AI_ACTIVITY_RECORD::m_ArgumentsJson`.
- Build the arguments payload with structured JSON construction instead of manual string assembly.
- Keep field names generic enough for PCB and schematic editors.

## Non-Goals

- Do not add high-frequency mouse motion/drag tracking in this slice.
- Do not infer selected object identities here; object identity remains part of context snapshots.
- Do not change action names such as `mouse.click` or existing selection/move command names.

## Contract

Mouse activity arguments include:

- `category`: existing event category string.
- `action`: existing event action string.
- `x`, `y`: rounded world coordinates when the event has a position.
- `button`: primary button name for single-button mouse events.
- `buttons`: array of all active button names.
- `modifiers`: array of active modifier names in stable order: `shift`, `ctrl`, `alt`, `super`, `meta`, `altgr`.

Non-mouse events keep `category` and `action`; keyboard events may include `key_code` and `modifiers` when recorded in future.

## Acceptance

- Left click with Shift/Ctrl includes `button`, `buttons`, and ordered `modifiers`.
- Right click includes `button=right`.
- Existing command, selection, movement, and mouse-motion filtering behavior remains unchanged.
