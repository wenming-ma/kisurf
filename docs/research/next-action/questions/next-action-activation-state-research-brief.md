# KiSurf Next Action Activation-State Research Brief

## 1. Research Background

KiSurf is an AI-native PCB editor integrated inside the KiCad/KiSurf editor runtime. It is not a plugin-style external script, an MCP wrapper, or a detached chatbot panel. The Next Action Agent observes the engineer's real editor state and, when appropriate, proposes the next useful action as an in-workspace preview. The preview appears directly in the engineer's working surface and only materializes into the real project after explicit user acceptance.

This research brief is not about the overall Next Action architecture. The focused question is:

**For Placement, Routing, and Auto-fill/Refill, how should KiSurf decide when the Agent is in an active observation state, when a hidden attempt is appropriate, and when a preview opportunity is ready?**

Some earlier designs relied on idle/debounce windows to decide when the Agent should trigger. For PCB routing and placement, however, the editor already exposes very clear user-action boundaries through its tool state machine. Pure timing heuristics may be unnecessary or even misleading for those workflows.

## 2. Core Hypotheses

This research should validate and refine the following hypotheses:

1. Routing and Placement active states should primarily come from editor tool-state transitions and user commit/cancel events, not from simple timing thresholds.
2. Auto-fill/Refill is less obvious than canvas tools and needs separate research around UI focus, editing transactions, IME composition, cell commit events, selection stability, and schema validation.
3. Time thresholds may still be useful as auxiliary signals for noise reduction, rate limiting, visual update throttling, or local stability checks, but they should not be the primary definition of Routing or Placement active state.

## 3. Routing Activation-State Research

The working intuition is:

> Interactive routing should be considered active from the moment the user starts routing until the route segment/path is committed, finished, canceled, double-clicked to finish, or the tool is switched. The entire in-progress route interaction should be treated as a routing active state.

Please research:

1. How do KiCad and similar EDA tools represent the internal state machine of interactive routing?
   - route start
   - route in progress
   - segment committed
   - corner placed
   - layer switch / via insertion
   - finish route
   - cancel route
   - tool switch

2. What should the Next Action Agent observe during routing active state?
   - current net / differential pair / bus
   - route start anchor
   - current cursor endpoint / tentative segment
   - committed route stub
   - possible target anchors
   - router mode, such as shove, walkaround, or highlight-collision

3. When should the Agent attempt to generate a preview during active routing?
   - after each committed route corner?
   - while hovering before the next corner is committed?
   - when the cursor pauses near a plausible target anchor?
   - when the route encounters an obstacle, failure, or obvious detour?
   - after the user commits a segment but the net remains unfinished?

4. When should the Agent not intervene?
   - the user is moving the cursor quickly
   - the tentative route is changing every frame
   - the router is actively shoving or dragging geometry
   - the user is very close to manually completing the route
   - the context is insufficient to produce a legal preview

5. Should routing use a two-layer state model?
   - `routing_active`: the user is inside the interactive routing tool, and the Agent may observe continuously.
   - `suggestion_ready`: the current route state is stable and meaningful enough to start a hidden attempt.

The research output should propose a routing state machine, key events, staleness conditions, and which events should cancel or supersede the current hidden attempt or preview.

## 4. Placement Activation-State Research

The working intuition is:

> Whether the user is placing a via, a shape, or a footprint, the period after entering the placement tool and before the actual placement click/commit should be considered placement active state.

Placement should be generalized to include:

- placing a footprint
- placing a via
- placing a shape or graphic item
- placing a zone outline or keepout
- moving or dragging an existing item to a new position

Please research:

1. How do KiCad and similar EDA tools represent the internal state machine of placement tools?
   - tool armed
   - item attached to cursor
   - preview follows cursor
   - rotate / flip / layer switch
   - click commit
   - multi-place repeat
   - cancel

2. Which events should start placement active state?
   - user selects a placement tool
   - user selects a footprint from a library or picker
   - user starts dragging or moving an existing item
   - user starts placing a via, shape, zone, or similar object

3. Which events should end placement active state?
   - user clicks to commit the placement
   - user cancels the tool
   - user switches tools
   - the object enters a board commit
   - in multi-place mode, should the active state continue after each commit?

4. When should the Agent generate a preview during placement active state?
   - an item is attached to the cursor and there are obvious candidate positions nearby
   - the user moves near an obstacle or conflict
   - the user has just placed a similar item and a pattern continuation exists
   - the user has selected a footprint whose local placement can be improved
   - the user hovers near a candidate anchor

5. Should placement use separate states?
   - `placement_active`: the placement tool is running.
   - `candidate_context_stable`: the current state can support a hidden attempt.
   - `preview_publishable`: the hidden attempt has passed render, validation, and LLM review.

The research output should propose a placement state machine, event boundaries, preview timing, multi-place handling, and ways to avoid interrupting the user's natural placement movement too early.

## 5. Auto-fill / Refill Activation-State Research

Auto-fill/Refill differs from Routing and Placement because it usually does not have a clear geometric tool lifecycle from start to placement click. It often occurs in property panels, tables, grids, dialogs, inspectors, BOM-like surfaces, netclass tables, or other typed UI surfaces.

Please research this area in more depth:

1. How should Auto-fill active state be defined?
   - focus enters an editable cell or field
   - the user selects a table row, column, or range
   - the user has typed a value but has not committed it yet
   - the user just committed a cell and nearby values suggest a refill pattern
   - the user opened a property dialog or panel

2. Which UI states should suppress triggering?
   - the user is actively typing
   - IME composition is still active
   - the user is selecting text
   - a dropdown or combo box is open
   - validation is still updating
   - focus is in a generic search box or a non-project-data field

3. Should Auto-fill be driven by commit events rather than pause timers?
   - cell edit committed
   - focus moved to the next cell
   - row/column selection changed
   - multi-cell data pasted
   - property value accepted

4. How should Auto-fill previews appear?
   - inline ghost value
   - highlighted cell diff
   - row/column/range patch preview
   - grouped field-bundle preview
   - keyboard and mouse accept/reject interaction

5. How should Auto-fill staleness be determined?
   - focused cell changed
   - table schema changed
   - selected range changed
   - source value changed
   - project context changed
   - validation rules changed

6. Should Auto-fill also use a layered state model?
   - `surface_active`: focus is inside a typed editable surface.
   - `edit_in_progress`: the user is typing, so the Agent should not interrupt.
   - `commit_observed`: a value has been confirmed and may create a refill opportunity.
   - `selection_stable`: the row, column, or range selection is stable enough.
   - `preview_publishable`: the patch has passed schema validation and LLM review.

The research output should especially focus on Auto-fill's event model, because it is the least obvious of the three work states.

## 6. Engineering Practices to Compare

Please compare interaction-state designs across:

- KiCad / pcbnew interactive routing and placement tools.
- EDA tools such as Altium, Allegro, EasyEDA, Fusion, or similar systems that expose routing/placement previews or interactive assistants.
- IDE inline-completion trigger and suppression logic, but only as an analogy, not as a direct model for PCB editing.
- Spreadsheet, property-grid, and table-editor auto-fill, copy-down, formula suggestion, and validation-preview mechanisms.
- CAD/EDA command-state, tool-state, modal interaction, and commit/cancel transaction patterns.

## 7. Expected Research Conclusions

The final report should answer:

1. Should Routing active state be primarily driven by interactive routing tool state rather than timing thresholds?
2. Should Placement active state be primarily driven by placement-tool armed state, item-attached state, and commit/cancel events?
3. What event model should Auto-fill/Refill use to distinguish active, editing, stable, and ready-to-suggest states?
4. For all three states, which events should trigger observation, which events should only update local context, and which events should cancel or supersede a hidden attempt?
5. What role should timing thresholds still play in each state?
6. What `AI_NEXT_ACTION_SCHEDULER` state machine is recommended?
7. Which signals should come from KiCad/KiSurf native tool state, and which signals require new instrumentation?

## 8. Important Constraints

KiSurf is an AI-native editor-integrated system, not a PydanticAI-style plugin, external IPC automation layer, or detached scripting wrapper. The model remains the semantic decision-maker. However, Routing, Placement, and Auto-fill active/stable states should come as much as possible from deterministic editor state machines and user commit/cancel events.

Deep Research should not treat the deterministic event detector as a decision-making Agent. It is only observation and scheduling infrastructure for the Next Action Agent.
