# AI Action Catalog Stable Order Design

Date: 2026-06-16

## Problem

`AI_ACTION_CATALOG::Build` currently returns descriptors in the action manager's native iteration
order and applies `aLimit` while iterating. The model-visible catalog can therefore vary with
container order and may truncate useful safe actions before the model sees them.

## Goals

- Make model-facing action catalogs deterministic.
- Prefer safer actions when a bounded catalog limit is applied.
- Keep existing action classification and descriptor content unchanged.

## Non-Goals

- No new action permissions.
- No new action classification keywords.
- No change to policy execution or allowlist behavior.

## Design

`AI_ACTION_CATALOG::Build` will:

1. Collect all valid descriptors from `ACTION_MANAGER`.
2. Sort descriptors by model-facing safety priority:
   - read-only
   - interactive
   - modifying
   - destructive
3. Sort descriptors with the same priority by action name, then by friendly name.
4. Apply `aLimit` after sorting.

This preserves the existing action discovery surface while making bounded provider context stable and
biased toward actions that are safest for model autonomy.

## Verification

- Add unit coverage that registers actions in a mixed order and verifies the returned catalog orders
  them by safety priority and name.
- Re-run `AiActionCatalog` and nearby AI context/provider tests.

## Self Review

- Sorting does not grant execution permission; execution remains governed by existing policy.
- Applying the limit after sorting avoids container-order truncation.
- Safety-priority ordering makes the default bounded context more useful for read-only inspection
  before mutation.
