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

KiSurf is currently a developer-preview branch with a first AI-native workflow
slice implemented. It is not a finished PCB layout/routing assistant yet, but
the current branch can build PCB and schematic editors with an Agent pane,
model-provider wiring, model tool interfaces, semantic panel state, and
observability logs.

## Direct Use Status

Short answer: you can try the current Agent workflow as a developer preview, but
it is not the finished AI-native product yet.

Currently implemented:

- Agent pane in PCB and schematic editors.
- OpenAI-compatible provider path with default base URL
  `https://sub2api.wenming-dev.org/v1`.
- Agent pane `Model...` settings entry for OpenAI-compatible base URL, model,
  and API key configuration, with API keys stored in the local platform secret
  store instead of project files or shell setup.
- Anthropic-compatible provider kind reserved in settings UI and persistence;
  runtime request handling for Anthropic is not implemented yet.
- Explicit offline stub mode with `KISURF_AI_PROVIDER=stub`.
- Python-first AI execution session runtime for PCB preview-first edits:
  session tools, typed atomic operations, semantic shadow board, operation
  journal, checkpoints, rollback, native preview rendering, validation, accept
  replay, and a local protobuf Python worker/SDK.
- PCB session startup seeds existing board tracks, vias, zones, drawing shapes,
  footprints, and pads into typed shadow-board handles, including selected pad
  metadata for agent queries.
- Successful Python mutation cells automatically produce step feedback through
  editor-native validation and preview services when the script did not request
  them explicitly, so iterative Agent work is visible before Accept.
- Python SDK composite helpers such as via rings and annular zones lower into
  typed atomic operations instead of model-facing bespoke PCB tools.
- Model-facing tools for actions, context snapshots, workspace view, visual
  frame metadata/pixels when available, activity timeline, AI execution session
  control/query/render/validation, and guarded semantic UI actions.
- Legacy model-facing composite/script tools such as
  `script_run_operation_bundle`, `pcb_fill_via_matrix`, and direct
  `pcb_create_*` tools are no longer exposed; complex editing goes through
  `kisurf_run_cell` and the session control tools.
- Session typed properties are inspectable and replayable: property patches
  merge into shadow-board state, invalid zone property patches are rejected
  before journal append, and accepted via/track/zone/shape property changes use
  native KiCad setters instead of geometry patching.
- Model-originated `kisurf_run_action` calls are preview-first: allowed action
  requests create a pending preview suggestion, and the native action runner is
  invoked only after explicit user Accept.
- Autonomous background suggestions are preview-only in the current session
  runtime phase: generated edit objects are stripped, preview remains
  available, and real board mutation must go through an accepted execution
  session.
- Active-routing route-to-anchor previews can target a semantic anchor directly;
  if the model omits the start anchor, KiSurf uses the current
  `tool.routing.start` anchor from the routing context.
- Active footprint-placement contexts expose semantic placement candidate anchors
  around the cursor, giving the model deterministic named points for placement
  previews instead of pixel guessing.
- Visual reads in active footprint-placement contexts automatically highlight
  placement candidate anchors, so the model sees layout choices without
  repeating visual directives.
- Anchor-focus operation previews render lightweight PCB canvas markers, giving
  semantic anchor choices a visible non-mutating board preview.
- Visual-frame diagnostics that report why pixels are unavailable instead of
  returning only an empty frame.
- Routing visual reads automatically carry render directives for the active
  route net/layer and routing anchors, so the model sees the intended visual
  focus without restating it on every frame request.
- User activity records for commands, selection, movement, and mouse clicks,
  including click coordinates, button, and modifier details.
- Agent observability log for user input, model input/output, tool calls, tool
  results, suggestions, and recent semantic panel state.
- Native semantic self-test surface for Agent pane controls and state, including
  a guarded bridge for model-requested Agent pane UI actions that cannot bypass
  human accept confirmation.

Still in progress:

- Full GUI smoke verification in the current environment.
- Broader in-memory canvas visual capture coverage and richer editor semantic
  trees beyond the Agent pane.
- Production-level autonomous placement/routing workflows.
- Model settings UI edge-case polish and production credential-store fallback
  handling.
- Dedicated Python worker event streaming during a running cell and final
  native DRC over reconstructed shadow-board state. Stale-session rebase is
  intentionally out of the current phase; stale accept is rejected by base hash.

## Quickstart: Agent Preview

Build the developer targets:

```powershell
cmd.exe /S /C '"C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 && cmake --build out\build\x64-release --target pcbnew --config Release && cmake --build out\build\x64-release --target eeschema --config Release'
```

Configure the provider from inside the editor:

1. Open `AI -> Model Settings...`.
2. Alternatively, open `AI -> Agent` and click `Model...` in the Agent pane.
3. Choose `OpenAI-compatible`.
4. Set the base URL, model, and API key.
5. Press OK; the Agent reloads the model provider for the next chat or
   background suggestion.

The default base URL is `https://sub2api.wenming-dev.org/v1`. API keys are stored
through the local platform secret store and are not written to project files.
Normal Agent runtime configuration is loaded from Model Settings; OpenAI API
key, base URL, and model environment variables are not used as default runtime
credentials. Use `KISURF_AI_PROVIDER=stub` only when you deliberately want the
offline deterministic provider.
The `Anthropic-compatible` option is visible for future switching, but this
developer-preview branch only implements OpenAI-compatible runtime calls.

For offline deterministic testing:

```powershell
Set-Item Env:KISURF_AI_PROVIDER 'stub'
```

Launch one of the built editors:

```powershell
.\tools\run_from_build.ps1 -BuildDir .\out\build\x64-release -App pcbnew
.\tools\run_from_build.ps1 -BuildDir .\out\build\x64-release -App eeschema
```

Do not launch the build-tree `.exe` files directly unless you have already
synced runtime DLLs into the build tree. On Windows, direct double-click launch
can fail with a missing `kicommon.dll` system-error dialog because sibling build
directories are not on the process PATH. If you prefer direct executable launch,
run `.\tools\post_build.ps1 -BuildDir .\out\build\x64-release -SyncBuildTree`
after building to copy runtime DLLs into the build output directories.

Open the Agent pane from `AI -> Agent`, type a request, and press Send. If no
API key is saved in Model Settings, the Agent reports a configuration diagnostic
instead of silently falling back to stub mode.

## Verification

The current branch has been checked with native common tests for provider
configuration, Agent pane semantics, workspace/context tooling, observability,
and editor target builds. The Agent pane also exposes a native semantic self-test
surface so core controls and state can be verified without relying on external
desktop automation.

For the direct-use developer preview path, run:

```powershell
$build = Resolve-Path .\out\build\x64-release
$env:KICAD_RUN_FROM_BUILD_DIR = '1'
$env:PATH = @(
    'D:\Tools\vcpkg\installed\x64-windows\bin',
    "$build\kicad",
    "$build\common",
    "$build\api",
    "$build\common\gal",
    "$build\pcbnew",
    "$build\eeschema",
    "$build\cvpcb",
    $env:PATH
) -join ';'
& "$build\qa\tests\common\qa_common.exe" --run_test=AiDirectUseSmoke
```

For GUI smoke, launch PCB Editor with `tools\run_from_build.ps1` and inspect the
real desktop window. This branch has been checked by opening `AI -> Agent` from
the build-tree PCB Editor and verifying that no missing-DLL/system-error modal is
shown during launch.

## Relationship to KiCad

KiSurf is built on the idea of extending and evolving the KiCad experience toward an
AI-native workflow. KiCad is an open source EDA platform and the foundation this
project intends to build from.

## Contributing

The project direction is still being shaped. Contributions, discussions, and design
proposals are welcome once the initial architecture and development setup are in
place.
