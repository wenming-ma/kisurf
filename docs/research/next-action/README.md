# Next Action Research

Place research reports for the Next Action Agent here.

This folder is for the ambient workflow that observes the engineer's live editor
state and proposes the next useful action as an in-workspace preview.

The current architectural direction is a unified, LLM-mediated, observation-driven
runtime:

1. Observe the live workspace.
2. Let the LLM Agent decide whether a Next Action opportunity exists.
3. Let the LLM Agent call tools in shadow/scratch state.
4. Render, validate, and observe the result again.
5. Retry, roll back, or publish a user-visible preview.
6. Commit to the real board only after user accept.

Placement, routing, and auto-filling/refilling are work states inside the same
runtime. They are not separate agents, separate frameworks, or separate research
folders.

The implementation entry point for this direction is `AI_NEXT_ACTION_RUNTIME`:
native candidate generators, validators, and renderers are tools under the LLM
loop, and only an LLM review publish decision may create a user-visible preview.
