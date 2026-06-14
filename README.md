# KiSurf

**An AI-native PCB editor built on KiCad.**

KiSurf is an exploration of what KiCad could feel like if AI were designed into the
entire electronics design workflow from the beginning.

The goal is not to bolt a chatbot onto an EDA tool, wrap KiCad with an external MCP
server, or ship a thin plugin that can only see a narrow slice of the project. KiSurf
aims to make AI a first-class collaborator inside the schematic and PCB editing
experience itself.

## Vision

Modern PCB design is deeply contextual. Good decisions depend on schematic topology,
component intent, power domains, stackup constraints, net classes, layout geometry,
library choices, DRC/ERC results, manufacturing limits, revision history, and the
engineer's current task.

Most AI integrations only see fragments of that world.

KiSurf is built around a different premise: the assistant should understand the
active project as a living engineering workspace. It should be able to follow the
design as it changes, reason across schematic and board data, notice risks early,
and collaborate with the engineer in real time.

## Core Interaction: Suggest, Accept, Materialize

The core product goal is to make the next reasonable engineering step visible and
easy to accept.

KiSurf should feel like working with a senior hardware partner who is watching the
same schematic and PCB you are working on. As the engineer designs, the system
continuously proposes the next useful action. If the suggestion is right, the
engineer can accept it with a lightweight confirmation, similar to pressing Tab to
accept a coding suggestion, and the proposed work immediately materializes in the
schematic editor or PCB editor.

This is not just a chat response. A good suggestion should be represented as a
reviewable edit in the actual design workspace: a component placed on the
schematic, a net connection generated, a footprint positioned on the board, or a
route previewed and applied.

The ideal loop is:

1. KiSurf observes the current design context.
2. KiSurf suggests the next concrete design action.
3. The engineer reviews the suggestion in place.
4. The engineer accepts it with one action.
5. The schematic or PCB updates immediately.

## What AI-Native Means

For KiSurf, "AI-native" means the AI is part of the editor's core workflow, not an
afterthought.

- **Deep project awareness**: the assistant can reason over schematic symbols,
  footprints, nets, constraints, board geometry, rule checks, and design history.
- **Workflow-level collaboration**: AI support spans schematic capture, constraint
  definition, component selection, PCB placement, routing review, manufacturing
  checks, and documentation.
- **Real-time design context**: suggestions are based on the current state of the
  user's project, not a stale export or disconnected prompt.
- **Engineer-in-control editing**: the AI proposes, explains, and previews changes;
  the engineer decides what becomes part of the design.
- **One-action acceptance**: useful suggestions are not trapped in text; they can
  become concrete schematic or PCB edits after engineer approval.
- **Native interaction surfaces**: assistance should appear where the work happens:
  inspectors, canvases, rule editors, design review panels, command palettes, and
  contextual workflows.

## Not Just a Plugin

KiSurf is intentionally not centered on a plugin-style AI assistant.

A plugin can be useful, but it is usually limited by what the host application
chooses to expose. A truly AI-native PCB editor needs richer access to the internal
design model, editing state, validation pipeline, and user intent.

KiSurf's long-term direction is therefore deeper integration with KiCad's design
environment, so AI can participate across the full path from idea to manufactured
board.

## Example Capabilities

KiSurf is early, but these are the kinds of workflows it is meant to support:

### Schematic Design

- Suggest which components should be added next based on circuit intent,
  electrical requirements, library availability, and surrounding design context.
- Place schematic symbols in reasonable locations so the circuit structure remains
  readable as it grows.
- Generate net connections between components and explain the intended electrical
  relationship.
- Review the schematic continuously for missing pull-ups, suspicious power
  connections, inconsistent decoupling, or unclear signal intent.

### PCB Design

- Suggest footprint placement based on schematic relationships, connector
  orientation, signal flow, power integrity, thermal zones, mechanical boundaries,
  and routing pressure.
- Preview board-level placement changes directly on the PCB canvas before they are
  accepted.
- Propose and apply routing actions, from local trace completion to broader routing
  plans, while respecting constraints and manufacturability.
- Review PCB layout changes continuously instead of waiting for a final DRC pass.

### Design Review and Documentation

- Explain why a design rule violation matters and propose concrete, reviewable
  fixes.
- Help define design constraints from the schematic, datasheets, interface
  standards, and board-level intent.
- Generate and maintain engineering documentation from the actual project state.

## Design Principles

- **Hardware correctness comes first.** AI should reduce engineering risk, not hide
  it behind confident prose.
- **Suggestions must be inspectable.** Every proposed change should be explainable,
  reviewable, and reversible.
- **Accepting a suggestion should do real work.** The product should move beyond
  advice and turn approved suggestions into concrete, visible editor actions.
- **The editor remains deterministic.** AI can assist decisions, but the underlying
  project model must stay explicit and auditable.
- **Context beats chat.** The most valuable assistant is the one that understands
  what the engineer is doing without forcing constant manual prompting.
- **KiCad compatibility matters.** KiSurf builds on KiCad's ecosystem rather than
  replacing the open EDA foundation that already exists.

## Roadmap

The project is at the beginning. A possible early roadmap:

1. Establish the KiCad-based development foundation.
2. Define the AI-native architecture and integration boundaries.
3. Build a project context layer that can represent schematic, PCB, rules, symbols,
   footprints, and design state in a form useful for AI reasoning.
4. Prototype the suggest-accept-materialize interaction loop inside the editor.
5. Add schematic workflows for component selection, symbol placement, and automatic
   net generation.
6. Add PCB workflows for footprint placement, layout guidance, and automatic
   routing assistance.
7. Develop a reviewable change system for AI-suggested edits.

## Status

KiSurf is currently an early-stage project. The README defines the product direction
before the implementation begins.

## Relationship to KiCad

KiSurf is built on the idea of extending and evolving the KiCad experience toward an
AI-native workflow. KiCad is an open source EDA platform and the foundation this
project intends to build from.

## Contributing

The project direction is still being shaped. Contributions, discussions, and design
proposals are welcome once the initial architecture and development setup are in
place.
