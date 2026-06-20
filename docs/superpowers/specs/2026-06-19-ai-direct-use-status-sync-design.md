# AI Direct-Use Status Sync Design

## Problem

The README direct-use section is the easiest way to answer whether the current branch can be tried directly. Recent foundation slices added direct-use smoke coverage, visual unavailable reasons, richer user input activity, panel-fill previews, and semantic UI confirmation metadata, but the README still describes the older surface.

## Goals

- Keep the README status honest: developer preview, not finished product.
- Add the newly implemented model-facing and observability capabilities.
- Keep quickstart instructions focused and avoid exposing secrets.
- Preserve the pending GUI smoke caveat.

## Non-Goals

- Do not claim production readiness.
- Do not add live API keys or local user-specific credentials.
- Do not replace the deeper architecture docs.

## Acceptance

- README lists the current tool/preview coverage including panel fill and route-to-anchor.
- README mentions visual unavailable diagnostics and enriched click/modifier activity.
- README mentions semantic accept confirmation guard.
- Verification still points to `AiDirectUseSmoke` and notes GUI smoke is pending.
