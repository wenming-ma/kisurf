# AI Direct Use Status Quickstart Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make the repository accurately answer whether KiSurf can be used directly today.

**Architecture:** This is a documentation-only slice. Update `README.md` to separate long-term product vision from current developer-preview status, provider setup, launch steps, verification evidence, and limitations.

**Tech Stack:** Markdown, existing KiSurf build layout, existing AI provider environment variables, git/rg checks.

---

## File Structure

- Modify: `README.md`
  - Add direct-use status, quickstart, and verification sections.
- Modify: `docs/superpowers/plans/2026-06-19-ai-direct-use-status-quickstart-implementation.md`
  - Record completed steps and verification evidence.

## Task 1: README Direct-Use Status

**Files:**
- Modify: `README.md`

- [x] **Step 1: Replace stale status text**

Replace the existing `## Status` body with a developer-preview status that says:

```markdown
KiSurf is currently a developer-preview branch with a first AI-native workflow
slice implemented. It is not a finished PCB layout/routing assistant yet, but
the current branch can build PCB and schematic editors with an Agent pane,
model-provider wiring, model tool interfaces, semantic panel state, and
observability logs.
```

- [x] **Step 2: Add Direct Use Status section**

Add this section after `## Status`:

```markdown
## Direct Use Status

Short answer: you can try the current Agent workflow as a developer preview, but
it is not the finished AI-native product yet.

Currently implemented:

- Agent pane in PCB and schematic editors.
- OpenAI-compatible provider path with default base URL
  `https://sub2api.wenming-dev.org/v1`.
- `OPENAI_API_KEY` runtime configuration and explicit offline stub mode with
  `KISURF_AI_PROVIDER=stub`.
- Model-facing tools for actions, context snapshots, workspace view, visual
  frame metadata/pixels when available, activity timeline, move preview, copper
  zone preview, and route-to-anchor preview.
- Agent observability log for user input, model input/output, tool calls, tool
  results, suggestions, and recent semantic panel state.
- Native semantic self-test surface for Agent pane controls and state.

Still in progress:

- Full GUI smoke verification in the current environment.
- Broader in-memory canvas visual capture coverage and richer editor semantic
  trees beyond the Agent pane.
- Production-level autonomous placement/routing workflows.
- Credential storage and end-user settings UI.
```

- [x] **Step 3: Add Quickstart section**

Add this section after `## Direct Use Status`:

```markdown
## Quickstart: Agent Preview

Build the developer targets:

```powershell
cmd.exe /S /C '"C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 && cmake --build out\build\x64-release --target pcbnew --config Release && cmake --build out\build\x64-release --target eeschema --config Release'
```

Configure the provider in your user environment or current shell:

```powershell
Set-Item Env:OPENAI_API_KEY '<your key>'
```

Optional provider settings:

```powershell
$env:KISURF_AI_BASE_URL = 'https://sub2api.wenming-dev.org/v1'
$env:KISURF_AI_MODEL = 'gpt-4.1-mini'
```

For offline deterministic testing:

```powershell
$env:KISURF_AI_PROVIDER = 'stub'
```

Launch one of the built editors:

```powershell
.\out\build\x64-release\pcbnew\pcbnew.exe
.\out\build\x64-release\eeschema\eeschema.exe
```

Open the Agent pane from the editor's AI/Agent menu entry, type a request, and
press Send. If `OPENAI_API_KEY` is missing, the Agent reports a configuration
diagnostic instead of silently falling back to stub mode.
```

- [x] **Step 4: Add Verification section**

Add this section after quickstart:

```markdown
## Verification

The current branch has been checked with native common tests for provider
configuration, Agent pane semantics, workspace/context tooling, observability,
and editor target builds. The Agent pane also exposes a native semantic self-test
surface so core controls and state can be verified without relying on external
desktop automation.

External GUI smoke is still pending in this environment. Treat manual GUI use as
developer-preview validation, not release evidence.
```

## Task 2: Verify Documentation

**Files:**
- Modify: `README.md`
- Modify: this plan file

- [x] **Step 1: Run markdown/diff checks**

Run:

```powershell
git diff --check
```

Expected: exits 0.

Observed: `git diff --check` exited 0.

- [x] **Step 2: Run secret scan**

Run:

```powershell
rg -n "sk-[0-9A-Za-z_-]{20,}|OPENAI_API_KEY\s*=|KISURF_AI_API_KEY\s*=" README.md docs\superpowers\specs\2026-06-19-ai-direct-use-status-quickstart-design.md docs\superpowers\plans\2026-06-19-ai-direct-use-status-quickstart-implementation.md
```

Expected: no matches. The README must mention `OPENAI_API_KEY` without assignment syntax.

Observed: secret scan returned no matches.

- [x] **Step 3: Update plan evidence**

Record actual check results under this task and check off completed steps.

- [x] **Step 4: Commit**

Stage only files from this slice:

```powershell
git add README.md docs/superpowers/specs/2026-06-19-ai-direct-use-status-quickstart-design.md docs/superpowers/plans/2026-06-19-ai-direct-use-status-quickstart-implementation.md
git commit -m "docs: clarify ai direct use status"
```

Observed: committed as `fd62dba7 docs: clarify ai direct use status`.

## Self-Review

- Spec coverage: The plan maps every spec goal to a concrete README section.
- Placeholder scan: No TBD/TODO markers.
- Scope check: Documentation-only; no C++ behavior changes.
- Safety check: No real key values are included, and assignment-pattern secret
  scan is required before commit.
