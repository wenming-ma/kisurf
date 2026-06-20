# AI Routing Visual Default Focus Design

## Purpose

When the Agent reads the current workspace while the PCB router is active, the visual section should already describe the important routing emphasis. The model should not have to rediscover or restate the current net and layer every time it asks for the visual frame. KiSurf already exposes explicit `render_directives`; this slice adds routing-aware defaults for active routing contexts.

## Scope

This slice updates the read-only visual surfaces:

- `kisurf_get_visual_frame`
- the nested `visual` section of `kisurf_get_workspace_view`

When the current context is PCB `RoutingTrack` and the tool-state JSON contains route `net` and `layer`, default visual responses include:

- `render_directives.focus_layer`
- `render_directives.focus_net`
- `render_directives.dim_unfocused_layers = true`
- `render_directives.highlight_anchor_ids` for positional routing anchors in the current snapshot, bounded to 32 IDs

Explicit visual parameters still override defaults:

- A supplied `focus_layer` replaces the routing layer default.
- A supplied `focus_net` replaces the routing net default.
- A supplied `dim_unfocused_layers` value wins over the routing default.
- Supplied `highlight_anchor_ids` wins over automatic routing-anchor highlighting.

Non-routing contexts keep the existing compact behavior and omit `render_directives` unless the caller supplies directive parameters.

## Routing Anchor Selection

Automatic routing highlights include positional anchors whose kind is routing-related:

- `RouteStart`
- `RouteTarget`
- `RouteCandidate`
- `OrthogonalBreakout`
- `FortyFiveIntersection`

The order follows the current snapshot anchor order. This preserves deterministic ordering from the existing context-anchor provider and avoids inventing visual sorting policy in the semantic tool handler.

## Error Handling

Malformed explicit visual arguments keep existing fail-closed behavior. Automatic routing defaults do not introduce new denial modes:

- Invalid or missing routing tool-state JSON simply means no automatic defaults.
- Missing net or layer simply means no automatic focus directive.
- Non-positional routing anchors are ignored for automatic highlights.
- Automatic highlight IDs are generated from already-valid current anchors, so they do not need the explicit validation error path.

## Tests

Add focused common tests:

- `kisurf_get_visual_frame` with `{}` in an active routing context returns focus layer, focus net, dim flag, and automatic routing highlight IDs.
- `kisurf_get_workspace_view` with default `{}` carries the same default routing render directives in `workspace_view.visual`.
- A non-routing visual read still omits `render_directives` by default.
- Explicit visual directives continue to work.

## Self-Review

- Placeholder scan: no TBD or TODO markers.
- Consistency check: this extends the existing declarative render-directive contract without claiming pixels are drawn yet.
- Scope check: this is a semantic visual contract change, not a canvas renderer implementation.
- Ambiguity check: routing defaults require PCB `RoutingTrack` and both net and layer in the current tool-state JSON.
