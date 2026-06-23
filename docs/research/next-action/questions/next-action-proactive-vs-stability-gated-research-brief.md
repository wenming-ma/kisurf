# KiSurf Next Action: Proactive Next-Landing vs Stability-Gated Preview Research Brief

## 1. Research Goal

KiSurf is an AI-native PCB editor built inside the KiCad/KiSurf editor runtime. The Next Action Agent is not a plugin, external IPC automation layer, or detached chatbot. It observes the engineer's live workspace and proposes the next useful concrete action as an in-workspace preview. The preview only becomes a real board edit after explicit user acceptance.

This research brief asks Deep Research to compare two possible scheduling and preview-generation strategies for Routing and Placement:

1. **Stability-gated preview strategy**
2. **Proactive next-landing strategy**

The goal is to determine which strategy is better for KiSurf's AI-native Next Action experience, whether they should be combined, and how the scheduler should be designed.

## 2. Strategy A: Stability-Gated Preview

This is the strategy suggested by the previous activation-state research.

For routing:

- The editor enters `routing_active` when the interactive routing tool starts.
- The Agent observes continuously while routing is active.
- The Agent starts hidden attempts mainly at stable semantic boundaries:
  - after a route corner or segment is committed
  - after route attempt-finish fails
  - after continue-from-end changes the route head
  - when the cursor dwells near a plausible anchor
  - when route state is stable enough to avoid thrashing
- Raw cursor motion mostly updates local context and suppresses hidden attempts.

For placement:

- The editor enters `placement_active` when a placement tool is armed or an item is attached to the cursor.
- The Agent observes continuously while placement is active.
- The Agent starts hidden attempts when candidate context becomes stable:
  - item identity is unchanged
  - transform basis is stable
  - cursor is not sweeping quickly
  - local anchors/conflicts are coherent
  - user appears to be evaluating rather than merely moving

The main advantage of Strategy A is that it avoids thrashing, avoids distracting previews during fast motion, and aligns with conventional interactive-tool stability boundaries. The possible weakness is that it may be too conservative for an AI-native "next action" assistant, because it waits for the user or cursor context before planning.

## 3. Strategy B: Proactive Next-Landing

This is the alternative mechanism proposed by the product direction.

For routing:

1. The user starts routing and clicks the route start point.
2. The active route head is now known: current net, start anchor, current layer, design rules, and route context.
3. While the user is moving the mouse to look for the next fixed point, the Agent immediately begins planning the next recommended fixed landing point.
4. The Agent does **not** need to predict where the user's mouse is about to click.
5. The Agent's task is to propose where the next fixed route point should be from the current route head.
6. If the user manually clicks a point before the Agent publishes a preview, the in-flight suggestion is discarded.
7. The route head advances to the user's committed point, and the Agent starts the next planning cycle from the new route head.

For placement:

1. The user enters a placement tool or attaches an item to the cursor.
2. The active placement object is known: footprint/via/shape/zone, layer/side, rules, local geometry, and design intent.
3. The Agent immediately begins planning a recommended landing location, orientation, or local correction.
4. The Agent does **not** need to infer the exact current mouse destination.
5. If the user commits placement before the Agent publishes a preview, the in-flight suggestion is discarded.
6. The Agent starts a new cycle from the newly committed object/location if the tool remains active.

In this strategy, the Agent is not a cursor predictor. It is a proactive planner of the next semantic design action. Mouse position may be optional context, but it is not the main trigger or target.

The possible advantage of Strategy B is that it is more AI-native and more aligned with "Next Action": the system proposes the next design step, instead of waiting to see what the user might be trying to do. The possible weakness is that it may generate previews that compete with the user's current hand motion if the publication policy is not carefully designed.

## 4. Core Comparison Questions

Please compare Strategy A and Strategy B across the following dimensions:

1. **User experience**
   - Which strategy feels more helpful during fast PCB routing or placement?
   - Which strategy is less likely to interrupt or fight the engineer's hand?
   - Which strategy better matches a "Tab-to-accept next action" workflow?

2. **AI-native product fit**
   - Which strategy better fits KiSurf's goal of proactive next-step suggestions?
   - Is Strategy A too conservative for a background Next Action Agent?
   - Does Strategy B better distinguish KiSurf from ordinary cursor-assist or inline preview tools?

3. **Technical feasibility**
   - Can Strategy B reliably plan next landing points without relying heavily on current cursor location?
   - What structured context is needed for Strategy B?
   - Does Strategy B require stronger routing/placement tools than Strategy A?

4. **Scheduling complexity**
   - Which strategy produces simpler scheduler semantics?
   - Which strategy is easier to cancel/supersede correctly?
   - How should in-flight hidden attempts be canceled when the user manually commits before preview publication?

5. **Preview quality**
   - Does waiting for cursor stability improve preview relevance?
   - Does proactive planning produce better design-quality suggestions because it is not biased by current mouse motion?
   - Should the Agent propose a fixed landing point, a set of candidate anchors, or a complete short route/placement action?

6. **Latency tolerance**
   - If planning takes 200-800 ms, which strategy handles latency better?
   - Under Strategy B, should the Agent continuously compute in the background and publish only if still relevant?
   - Under Strategy A, is the Agent more likely to miss the useful moment because it waits for stability?

7. **Trust and acceptance**
   - Which strategy is more likely to produce previews the user accepts?
   - How should rejected or ignored proactive previews affect future suggestions?
   - Should previews be silent unless confidence is high?

## 5. Important Distinction: Planning Target vs Cursor Target

Please pay special attention to this distinction:

Strategy B does **not** ask the Agent to predict where the user's cursor will go.

Instead, Strategy B asks:

> Given the current route head or placement object, what is the next semantically good fixed point or landing action?

For routing, the current route head may be:

- active net
- route start anchor
- current committed route endpoint
- current layer
- allowed layers
- netclass rules
- candidate destination pads/vias/tracks
- local obstacles
- remaining airwire structure

For placement, the active object context may be:

- footprint/via/shape/zone identity
- layer/side
- orientation
- connected nets
- local obstacles
- board outline
- keepouts
- candidate anchors
- symmetry or repeated-placement patterns

Mouse position can still be used as weak context, but the proposed next action should not be defined as "where the mouse appears to be going."

## 6. Possible Hybrid Design

Please also investigate whether the best answer is a hybrid:

- Use deterministic tool state to enter `routing_active` or `placement_active`.
- Immediately start a proactive planning attempt once the route head or placement object is known.
- Continue updating observation as the user moves.
- Do not publish immediately if the user is moving too fast or if the preview would visually fight the current interaction.
- Publish only when:
  - the proposed next landing remains valid,
  - the user has not already committed a newer point,
  - the preview can be displayed without stealing focus,
  - the result passes validation and LLM review.
- If the user manually commits first, cancel the current attempt and restart from the new route head or object state.

In this hybrid, active-state detection is deterministic, planning is proactive, and publication is stability-aware.

Please evaluate whether this hybrid dominates both pure Strategy A and pure Strategy B.

## 7. Routing-Specific Research Questions

1. In KiCad's interactive router, after the route starts, is the current route head sufficient to plan candidate next fixed points without waiting for cursor dwell?
2. Should the Agent plan:
   - a single next corner/landing anchor,
   - several ranked anchors,
   - a short route segment to the next anchor,
   - or a route-to-destination attempt?
3. Should planning restart after every user-committed corner?
4. Which native router events should cancel the current proactive attempt?
   - user commits a segment
   - undo last segment
   - switch layer
   - via placement begins or is canceled
   - route finishes
   - route cancels
   - tool switches
5. How should the preview appear?
   - marker at proposed next landing point
   - short ghost segment
   - route-to-anchor overlay
   - ranked anchor labels
6. Should accept mean "commit the proposed next point" or "adopt the proposed short route segment"?

## 8. Placement-Specific Research Questions

1. Once an object is attached to the cursor, is the object context sufficient to plan a landing location without relying on current cursor position?
2. Should the Agent plan:
   - a landing point,
   - a local nudge,
   - an orientation/flip recommendation,
   - a snap-to-anchor action,
   - or a complete placement action?
3. Should planning restart after every user commit if the placement tool remains armed?
4. Which placement events should cancel the current proactive attempt?
   - user commits placement
   - user cancels
   - tool switches
   - object rotates/flips
   - layer/side changes
   - selected item changes
5. How should the preview avoid fighting the user's current hand movement?
   - delay visual publication but not hidden planning?
   - show only an anchor marker until stable?
   - publish only near high-confidence targets?
   - require explicit accept for all geometry-changing previews?

## 9. Auto-fill Relevance

Auto-fill/Refill is not the main focus of this comparison, but the report should briefly state whether the same proactive-vs-stability distinction applies.

Likely hypothesis:

- Auto-fill should remain commit/event driven.
- It should not plan from arbitrary typing motion.
- It should trigger after value commit, focus movement, range selection stabilization, paste completion, or schema-aware pattern evidence.

Please validate or correct this hypothesis.

## 10. Expected Output

The final Deep Research report should provide:

1. A clear comparison table between Strategy A and Strategy B.
2. A recommendation: A, B, or hybrid.
3. A proposed scheduler state machine for the recommended approach.
4. A routing-specific event lifecycle.
5. A placement-specific event lifecycle.
6. Cancellation/supersession rules.
7. Preview publication rules.
8. Required KiCad/KiSurf instrumentation.
9. Risks and mitigations.
10. A concise implementation direction for `AI_NEXT_ACTION_SCHEDULER`.

## 11. Constraints

Keep the following KiSurf constraints in mind:

- KiSurf is AI-native and editor-integrated, not a plugin or external IPC automation layer.
- The model is the semantic decision-maker.
- Deterministic tool-state detection is scheduler infrastructure, not a decision-making Agent.
- All real edits must remain preview-first and accept-gated.
- The preview appears in the real workspace, not in the chat panel.
- If the user manually commits before an AI preview is published, the AI attempt should be canceled and restarted from the new state.
- The system should propose useful next actions, not merely infer where the user's cursor is moving.
