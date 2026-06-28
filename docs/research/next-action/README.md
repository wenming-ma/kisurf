# Next Action Research

Place research reports and external research prompts for the Next Action Agent here.

The cross-cutting KiSurf AI Native system architecture now lives outside
this folder:

- `../ai-native/results/kisurf-ai-native-system-synthesis-architecture-and-implementation-roadmap.md`
- `results/next-action-source-spike-20260622.md`

Research questions / briefs live in:

- `questions/`

Next Action-specific research results live in:

- `results/`

The latest research question briefs are:

- `questions/next-action-activation-state-research-brief.md`
- `questions/next-action-proactive-vs-stability-gated-research-brief.md`

The latest source-aware research results are:

- `results/next-action-implementation-strategy-pre-repo-access.md`
- `results/next-action-implementation-strategy-repository-aware.md`
- `results/KiSurf Next Action Activation State Research Brief.md`
- `results/LLM-Mediated Inner Loop for an AI-Native EDA Next Action Runtime.md`
- `results/next-action-source-spike-20260622.md`

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
