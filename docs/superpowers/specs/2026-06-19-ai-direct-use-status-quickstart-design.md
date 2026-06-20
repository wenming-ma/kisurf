# AI Direct Use Status Quickstart Design

Date: 2026-06-19

## Purpose

Answer the practical question "Can I use KiSurf directly now?" in the repository
itself. The current README still says implementation has not begun, while the
branch now contains a first AI-native slice: Agent pane, provider configuration,
workspace/context/visual/activity tools, semantic panel state, and native tests.
That mismatch makes direct use confusing even when the build is healthy.

## Source Observations

- `README.md` describes the vision but still says the implementation has not
  begun.
- `AI_PROVIDER_SETTINGS::DefaultBaseUrl()` already defaults to
  `https://sub2api.wenming-dev.org/v1`.
- `AI_PROVIDER_SETTINGS::FromEnvironment()` reads `OPENAI_API_KEY`,
  `KISURF_AI_BASE_URL`, `OPENAI_BASE_URL`, `KISURF_AI_MODEL`, and
  `OPENAI_MODEL`.
- `MakeDefaultAiProvider()` uses the OpenAI-compatible provider by default and
  preserves explicit offline mode with `KISURF_AI_PROVIDER=stub`.
- PCB and schematic editors expose a top-level AI/Agent entry and Agent pane
  tests cover the shared action and menu wiring.
- Native semantic self-test coverage exists, but external GUI smoke remains
  unverified because Computer Use was unavailable in the current environment.

## Goals

1. Make README status honest: KiSurf is no longer "before implementation"; it
   is a developer-preview AI-native slice, not a complete product.
2. Add a Direct Use section that tells a developer how to configure the provider,
   launch the built editors, open Agent, and use stub mode safely.
3. State known limitations without ambiguity:
   - GUI smoke is not yet verified in the current environment.
   - The system is not a finished layout/routing assistant yet.
   - Visual/context/tool interfaces exist, but deeper canvas and workflow
     coverage remain active development.
4. Include a verification section naming the tested targets and the native
   semantic self-test path.
5. Avoid storing or echoing any real API key.

## Non-Goals

- No C++ behavior changes.
- No Windows environment editing from the repository.
- No credential storage.
- No claim that the entire AI-native PCB product is complete.
- No GUI automation implementation in this slice.

## README Shape

Add these sections near the existing status area:

- `## Direct Use Status`
  - Clear answer: usable as a developer preview, not complete.
  - What works now: Agent pane in PCB/schematic, real provider path, stub mode,
    model tool interfaces, semantic panel state, observability logs.
  - What is not finished: GUI smoke, full visual canvas coverage, autonomous
    layout/routing workflows.
- `## Quickstart: Agent Preview`
  - Build targets from the current build tree.
  - Set `OPENAI_API_KEY` in the user environment or current shell.
  - Optional env vars: `KISURF_AI_BASE_URL`, `OPENAI_BASE_URL`,
    `KISURF_AI_MODEL`, `OPENAI_MODEL`, `KISURF_AI_PROVIDER=stub`.
  - Launch `out/build/x64-release/pcbnew/pcbnew.exe` or
    `out/build/x64-release/eeschema/eeschema.exe`.
  - Open the Agent pane from the editor AI/Agent menu entry.
  - Type a request and press Send.
- `## Verification`
  - Summarize the relevant native checks and the fact that GUI smoke remains
    pending.

## Acceptance Criteria

- README directly answers whether KiSurf can be used now.
- README includes concrete commands/paths for developer-preview use.
- README explains provider environment variables without including secrets.
- README names stub mode for offline testing.
- README does not overclaim full product completeness.
- `git diff --check` exits 0.
- Secret scan has no matches.

## Self-Review

- Placeholder scan: No TBD/TODO placeholders.
- Scope check: Documentation-only slice, focused on direct-use clarity.
- Safety check: Mentions `OPENAI_API_KEY` as an environment variable only; no
  key value is included.
- Ambiguity check: "Usable now" is defined as developer-preview use, not full
  product readiness.
