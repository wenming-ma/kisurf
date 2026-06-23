import contextlib
import io
import json
import math
import traceback
from typing import Any


SDK_NAME = "kisurf-ai-session-sdk"
__version__ = "0.1.0"
PROTOCOL_VERSION = "kisurf.ai.session.v1"


class KiSurfSession:
    def __init__(self, request: dict[str, Any] | None = None,
                 event_sink: Any | None = None) -> None:
        self._request = request or {}
        self._event_sink = event_sink
        self._operations: list[dict[str, Any]] = []
        self._events: list[dict[str, Any]] = []
        self._step_label = ""

    @contextlib.contextmanager
    def step(self, label: str):
        previous_label = self._step_label
        self._step_label = label
        try:
            yield self
        finally:
            if not self._step_label:
                self._step_label = previous_label

    def emit(self, kind: str, **arguments: Any) -> None:
        self._operations.append({"kind": kind, "arguments": _clean(arguments)})

    def event(self, kind: str, *, message: str = "",
              payload: dict[str, Any] | list[Any] | None = None) -> None:
        event = {
            "kind": str(kind),
            "message": str(message),
            "payload": _clean(payload or {}),
        }
        self._events.append(event)

        if self._event_sink:
            self._event_sink(event)

    def checkpoint(self, name: str) -> None:
        self.emit("session.checkpoint", name=name)

    def rollback_to(self, checkpoint_id: int) -> None:
        self.emit("session.rollback_to", checkpoint_id=int(checkpoint_id))

    def create_via(self, *, position: Any, net: str = "", diameter: Any = None,
                   drill: Any = None, layer_pair: Any = None, alias: str = "",
                   metadata: dict[str, Any] | None = None) -> None:
        args: dict[str, Any] = {"position": position}
        _set_optional(args, "net", net)
        _set_optional(args, "diameter", diameter)
        _set_optional(args, "drill", drill)
        _set_optional(args, "layer_pair", layer_pair)
        _set_optional(args, "alias", alias)
        _set_optional(args, "metadata", metadata)
        self.emit("pcb.create_via", **args)

    def create_track_segment(self, *, start: Any, end: Any, layer: str = "",
                             net: str = "", width: Any = None, alias: str = "",
                             metadata: dict[str, Any] | None = None) -> None:
        args: dict[str, Any] = {"start": start, "end": end}
        _set_optional(args, "layer", layer)
        _set_optional(args, "net", net)
        _set_optional(args, "width", width)
        _set_optional(args, "alias", alias)
        _set_optional(args, "metadata", metadata)
        self.emit("pcb.create_track_segment", **args)

    def create_track_polyline(self, *, points: list[Any], layer: str = "",
                              net: str = "", width: Any = None, alias: str = "") -> None:
        args: dict[str, Any] = {"points": points}
        _set_optional(args, "layer", layer)
        _set_optional(args, "net", net)
        _set_optional(args, "width", width)
        _set_optional(args, "alias", alias)
        self.emit("pcb.create_track_polyline", **args)

    def create_zone(self, *, outline: Any, layer_set: Any = None, net: str = "",
                    clearance: Any = None, priority: Any = None, fill_mode: str = "",
                    alias: str = "", metadata: dict[str, Any] | None = None) -> None:
        args: dict[str, Any] = {"outline": outline}
        _set_optional(args, "layer_set", layer_set)
        _set_optional(args, "net", net)
        _set_optional(args, "clearance", clearance)
        _set_optional(args, "priority", priority)
        _set_optional(args, "fill_mode", fill_mode)
        _set_optional(args, "alias", alias)
        _set_optional(args, "metadata", metadata)
        self.emit("pcb.create_zone", **args)

    def create_shape(self, *, shape_type: str, geometry: Any, layer: str = "",
                     width: Any = None, fill: Any = None, alias: str = "",
                     metadata: dict[str, Any] | None = None) -> None:
        args: dict[str, Any] = {"shape_type": shape_type, "geometry": geometry}
        _set_optional(args, "layer", layer)
        _set_optional(args, "width", width)
        _set_optional(args, "fill", fill)
        _set_optional(args, "alias", alias)
        _set_optional(args, "metadata", metadata)
        self.emit("pcb.create_shape", **args)

    def move_items(self, *, handles: list[Any], delta: Any = None,
                   target_positions: Any = None) -> None:
        args: dict[str, Any] = {"handles": handles}
        _set_optional(args, "delta", delta)
        _set_optional(args, "target_positions", target_positions)
        self.emit("pcb.move_items", **args)

    def delete_items(self, *, handles: list[Any]) -> None:
        self.emit("pcb.delete_items", handles=handles)

    def update_item_geometry(self, *, handle: Any, geometry_patch: Any) -> None:
        self.emit("pcb.update_item_geometry", handle=handle, geometry_patch=geometry_patch)

    def set_item_net(self, *, handle: Any, net: str) -> None:
        self.emit("pcb.set_item_net", handle=handle, net=net)

    def set_item_layer(self, *, handle: Any, layer: Any = None,
                       layer_set: Any = None) -> None:
        args: dict[str, Any] = {"handle": handle}
        _set_optional(args, "layer", layer)
        _set_optional(args, "layer_set", layer_set)
        self.emit("pcb.set_item_layer", **args)

    def set_item_properties(self, *, handle: Any, typed_props: dict[str, Any]) -> None:
        self.emit("pcb.set_item_properties", handle=handle, typed_props=typed_props)

    def set_metadata(self, *, handle: Any, key_values: dict[str, Any]) -> None:
        self.emit("pcb.set_metadata", handle=handle, key_values=key_values)

    def refill_zones(self, *, handles: list[Any] | None = None,
                     affected_area: Any = None, all: bool = False) -> None:
        args: dict[str, Any] = {"all": all}
        _set_optional(args, "handles", handles)
        _set_optional(args, "affected_area", affected_area)
        self.emit("pcb.refill_zones", **args)

    def rebuild_connectivity(self, *, scope: Any = "session") -> None:
        self.emit("pcb.rebuild_connectivity", scope=scope)

    def run_validation(self, *, scope: Any = "session", level: str = "geometry") -> None:
        self.emit("pcb.run_validation", scope=scope, level=level)

    def apply_surface_patch(self, *, surface_id: str, patch: dict[str, Any],
                            table_id: str = "", target_scope: Any = None,
                            alias: str = "",
                            metadata: dict[str, Any] | None = None) -> None:
        args: dict[str, Any] = {
            "surface_id": surface_id,
            "patch": patch,
        }
        _set_optional(args, "table_id", table_id)
        _set_optional(args, "target_scope", target_scope)
        _set_optional(args, "alias", alias)
        _set_optional(args, "metadata", metadata)
        self.emit("surface.apply_patch", **args)

    def query_board_summary(self) -> None:
        self.emit("query.board_summary")

    def query_items(self, *, filter: dict[str, Any] | None = None) -> None:
        args: dict[str, Any] = {}
        _set_optional(args, "filter", filter)
        self.emit("query.items", **args)

    def query_item(self, *, handle: Any = None, alias: str = "") -> None:
        args: dict[str, Any] = {}
        _set_optional(args, "handle", handle)
        _set_optional(args, "alias", alias)
        self.emit("query.item", **args)

    def query_selection(self) -> None:
        self.emit("query.selection")

    def query_nets(self) -> None:
        self.emit("query.nets")

    def query_layers(self) -> None:
        self.emit("query.layers")

    def query_design_rules(self) -> None:
        self.emit("query.design_rules")

    def query_viewport(self) -> None:
        self.emit("query.viewport")

    def query_activity_timeline(self) -> None:
        self.emit("query.activity_timeline")

    def observe_step(self, *, step_id: int) -> None:
        self.emit("observe.step", step_id=step_id)

    def render_preview(self, *, region: Any = None, layer_mask: list[Any] | None = None,
                       mode: str = "native") -> None:
        args: dict[str, Any] = {}
        _set_optional(args, "region", region)
        _set_optional(args, "layer_mask", layer_mask)
        _set_optional(args, "mode", mode)
        self.emit("render.preview", **args)

    def create_via_ring(self, *, center: dict[str, float], radius: float, count: int,
                        net: str = "", diameter: Any = None, drill: Any = None,
                        layer_pair: Any = None, alias_prefix: str = "via",
                        metadata: dict[str, Any] | None = None) -> None:
        if count <= 0:
            raise ValueError("count must be positive")

        cx = float(center["x"])
        cy = float(center["y"])

        for index in range(count):
            theta = 2.0 * math.pi * index / count
            self.create_via(
                position={"x": cx + radius * math.cos(theta), "y": cy + radius * math.sin(theta)},
                net=net,
                diameter=diameter,
                drill=drill,
                layer_pair=layer_pair,
                alias=f"{alias_prefix}_{index}",
                metadata=metadata,
            )

    def create_annular_zone(self, *, center: dict[str, float], inner_radius: float,
                            outer_radius: float, segments: int = 64,
                            layer_set: Any = None, net: str = "",
                            clearance: Any = None, priority: Any = None,
                            fill_mode: str = "", alias: str = "",
                            metadata: dict[str, Any] | None = None) -> None:
        if segments < 3:
            raise ValueError("segments must be at least 3")
        if inner_radius <= 0 or outer_radius <= 0:
            raise ValueError("radii must be positive")
        if outer_radius <= inner_radius:
            raise ValueError("outer_radius must be greater than inner_radius")

        outline = {
            "type": "annulus",
            "center": {"x": float(center["x"]), "y": float(center["y"])},
            "inner_radius": float(inner_radius),
            "outer_radius": float(outer_radius),
            "outer": _circle_points(center=center, radius=outer_radius, count=segments),
            "inner": list(reversed(_circle_points(center=center, radius=inner_radius,
                                                  count=segments))),
        }

        self.create_zone(
            outline=outline,
            layer_set=layer_set,
            net=net,
            clearance=clearance,
            priority=priority,
            fill_mode=fill_mode,
            alias=alias,
            metadata=metadata,
        )

    def stitch_zone(self, *, center: dict[str, float], radius: float, count: int,
                    net: str = "", diameter: Any = None, drill: Any = None,
                    layer_pair: Any = None, alias_prefix: str = "stitch",
                    metadata: dict[str, Any] | None = None) -> None:
        self.create_via_ring(
            center=center,
            radius=radius,
            count=count,
            net=net,
            diameter=diameter,
            drill=drill,
            layer_pair=layer_pair,
            alias_prefix=alias_prefix,
            metadata=metadata,
        )

    def fanout_pad(self, *, pad_position: Any, via_position: Any, layer: str,
                   net: str = "", width: Any = None, via_diameter: Any = None,
                   via_drill: Any = None, layer_pair: Any = None,
                   alias_prefix: str = "fanout",
                   metadata: dict[str, Any] | None = None) -> None:
        self.create_track_segment(
            start=pad_position,
            end=via_position,
            layer=layer,
            net=net,
            width=width,
            alias=f"{alias_prefix}_escape",
            metadata=metadata,
        )
        self.create_via(
            position=via_position,
            net=net,
            diameter=via_diameter,
            drill=via_drill,
            layer_pair=layer_pair,
            alias=f"{alias_prefix}_via",
            metadata=metadata,
        )

    def to_result(self, *, ok: bool = True, error_code: str = "", message: str = "",
                  stdout: str = "", stderr: str = "") -> dict[str, Any]:
        return {
            "protocol": PROTOCOL_VERSION,
            "type": "cell_result",
            "sdk": {
                "name": SDK_NAME,
                "version": __version__,
                "protocol": PROTOCOL_VERSION,
            },
            "session": _clean(self._request.get("session", {})),
            "ok": ok,
            "error_code": error_code,
            "message": message,
            "stdout": stdout,
            "stderr": stderr,
            "step_label": self._step_label,
            "rollback_on_error": True,
            "events": self._events,
            "operations": self._operations,
        }


def run_cell(request_json: str, namespace: dict[str, Any] | None = None,
             event_sink: Any | None = None) -> str:
    request = json.loads(request_json)
    session = KiSurfSession(request, event_sink=event_sink)
    scope = namespace if namespace is not None else {}
    scope["session"] = session
    scope.setdefault("math", math)

    stdout = io.StringIO()
    stderr = io.StringIO()

    try:
        cell_text = request.get("cell", {}).get("text", "")
        with contextlib.redirect_stdout(stdout), contextlib.redirect_stderr(stderr):
            exec(cell_text, scope, scope)
    except KeyboardInterrupt:
        traceback.print_exc(file=stderr)
        return json.dumps(
            session.to_result(
                ok=False,
                error_code="python_cancelled",
                message="session cancelled",
                stdout=stdout.getvalue(),
                stderr=stderr.getvalue(),
            ),
            separators=(",", ":"),
        )
    except Exception as exc:
        traceback.print_exc(file=stderr)
        return json.dumps(
            session.to_result(
                ok=False,
                error_code="python_exception",
                message=str(exc),
                stdout=stdout.getvalue(),
                stderr=stderr.getvalue(),
            ),
            separators=(",", ":"),
        )

    return json.dumps(
        session.to_result(stdout=stdout.getvalue(), stderr=stderr.getvalue()),
        separators=(",", ":"),
    )


def _set_optional(target: dict[str, Any], key: str, value: Any) -> None:
    if value is not None and value != "":
        target[key] = value


def _circle_points(*, center: dict[str, float], radius: float, count: int) -> list[dict[str, float]]:
    cx = float(center["x"])
    cy = float(center["y"])
    return [
        {
            "x": cx + float(radius) * math.cos(2.0 * math.pi * index / count),
            "y": cy + float(radius) * math.sin(2.0 * math.pi * index / count),
        }
        for index in range(count)
    ]


def _clean(value: Any) -> Any:
    if isinstance(value, dict):
        return {str(key): _clean(inner) for key, inner in value.items()}
    if isinstance(value, (list, tuple)):
        return [_clean(inner) for inner in value]
    return value
