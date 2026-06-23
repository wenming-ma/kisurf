import argparse
import ctypes
import json
import os
import pathlib
import sys
import threading

if os.name == "nt":
    import msvcrt

from google.protobuf.message import DecodeError

from . import session_pb2
from .session import PROTOCOL_VERSION, run_cell


FRAME_PREFIX = b"KISURF_AI_FRAME_V1 "


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--request")
    parser.add_argument("--response")
    parser.add_argument("--stdio", action="store_true")
    parser.add_argument("--control-handle")
    parser.add_argument("--event-handle")
    args = parser.parse_args(argv)

    if args.stdio:
        return serve_stdio(args.control_handle, args.event_handle)

    if not args.request or not args.response:
        parser.error("--request and --response are required unless --stdio is used")

    request_path = pathlib.Path(args.request)
    response_path = pathlib.Path(args.response)

    request_json = request_path.read_text(encoding="utf-8")
    response_json = run_cell(request_json)
    response_path.write_text(response_json, encoding="utf-8")
    return 0


def serve_stdio(control_handle: str | None = None,
                event_handle: str | None = None) -> int:
    namespaces: dict[str, dict[str, object]] = {}
    control_state = _ControlState(namespaces, threading.get_ident())
    control_thread = _start_control_thread(control_handle, control_state)
    event_stream = _open_event_stream(event_handle) if event_handle else None

    try:
        while True:
            try:
                payload = _read_frame(sys.stdin.buffer)
            except Exception as exc:
                _write_response(_error_response("malformed_worker_frame", str(exc)))
                continue

            if payload is None:
                return 0

            if not payload:
                continue

            request = session_pb2.WorkerRequest()

            try:
                request.ParseFromString(payload)
            except DecodeError as exc:
                _write_response(_error_response("malformed_worker_request", str(exc)))
                continue

            response, should_exit = handle_worker_request(
                request,
                namespaces,
                control_state,
                event_stream,
            )
            _write_response(response)

            if should_exit:
                control_state.stop()
                return 0
    finally:
        if event_stream:
            event_stream.close()

    return 0


def handle_worker_request(
    request: session_pb2.WorkerRequest,
    namespaces: dict[str, dict[str, object]],
    control_state: "_ControlState | None" = None,
    event_stream=None,
) -> tuple[session_pb2.WorkerResponse, bool]:
    if request.protocol != PROTOCOL_VERSION:
        return _error_response(
            "unsupported_worker_protocol",
            f"Worker request must use {PROTOCOL_VERSION}.",
        ), False

    request_type = request.WhichOneof("request")

    if request_type == "shutdown":
        response = session_pb2.WorkerResponse(protocol=PROTOCOL_VERSION)
        response.shutdown_ack.SetInParent()
        return response, True

    if request_type == "cancel_session":
        session_id = str(request.cancel_session.session.id)
        namespaces.pop(session_id, None)

        if control_state:
            control_state.cancel_session(session_id)

        response = session_pb2.WorkerResponse(protocol=PROTOCOL_VERSION)
        _copy_session_context(response.cancel_session_ack.session,
                              request.cancel_session.session)
        response.cancel_session_ack.reason = request.cancel_session.reason
        return response, False

    if request_type != "run_cell":
        return _error_response(
            "unsupported_worker_request",
            f"Unsupported worker request type: {request_type!r}",
        ), False

    session_id = str(request.run_cell.session.id)
    namespace = namespaces.setdefault(session_id, {})
    legacy_request = _legacy_run_cell_request(request.run_cell)

    if control_state:
        control_state.begin_cell(session_id)

    try:
        response_json = run_cell(
            json.dumps(legacy_request, separators=(",", ":")),
            namespace=namespace,
            event_sink=(
                lambda event: _write_event(event_stream, event)
                if event_stream
                else None
            ),
        )
    finally:
        if control_state:
            control_state.end_cell(session_id)

    return _cell_result_response(json.loads(response_json)), False


def _read_frame(stream) -> bytes | None:
    header = stream.readline()

    if not header:
        return None

    if not header.startswith(FRAME_PREFIX):
        raise ValueError("worker frame has an unsupported header")

    try:
        payload_length = int(header[len(FRAME_PREFIX):].strip())
    except ValueError as exc:
        raise ValueError("worker frame has an invalid payload length") from exc

    if payload_length < 0:
        raise ValueError("worker frame payload length must be non-negative")

    payload = stream.read(payload_length)

    if len(payload) != payload_length:
        raise EOFError("worker frame payload is truncated")

    return payload


def _write_response(payload: session_pb2.WorkerResponse) -> None:
    payload_bytes = payload.SerializeToString()
    sys.stdout.buffer.write(FRAME_PREFIX)
    sys.stdout.buffer.write(str(len(payload_bytes)).encode("ascii"))
    sys.stdout.buffer.write(b"\n")
    sys.stdout.buffer.write(payload_bytes)
    sys.stdout.buffer.flush()


def _write_event(stream, event: dict[str, object]) -> None:
    payload = session_pb2.WorkerEvent()
    payload.kind = str(event.get("kind", ""))
    payload.message = str(event.get("message", ""))

    if "payload_json" in event:
        payload.payload_json = str(event.get("payload_json", "{}"))
    else:
        payload.payload_json = json.dumps(
            event.get("payload", {}),
            separators=(",", ":"),
        )

    payload_bytes = payload.SerializeToString()
    stream.write(FRAME_PREFIX)
    stream.write(str(len(payload_bytes)).encode("ascii"))
    stream.write(b"\n")
    stream.write(payload_bytes)
    stream.flush()


class _ControlState:
    def __init__(self, namespaces: dict[str, dict[str, object]],
                 main_thread_id: int) -> None:
        self._namespaces = namespaces
        self._main_thread_id = main_thread_id
        self._current_session_id: str | None = None
        self._running = True
        self._lock = threading.Lock()

    def begin_cell(self, session_id: str) -> None:
        with self._lock:
            self._current_session_id = session_id

    def end_cell(self, session_id: str) -> None:
        with self._lock:
            if self._current_session_id == session_id:
                self._current_session_id = None

    def cancel_session(self, session_id: str) -> None:
        should_interrupt = False

        with self._lock:
            self._namespaces.pop(session_id, None)
            should_interrupt = self._current_session_id == session_id

        if should_interrupt:
            _raise_async_exception(self._main_thread_id, KeyboardInterrupt)

    def stop(self) -> None:
        with self._lock:
            self._running = False

    def is_running(self) -> bool:
        with self._lock:
            return self._running


def _start_control_thread(
    control_handle: str | None,
    control_state: _ControlState,
) -> threading.Thread | None:
    if not control_handle:
        return None

    thread = threading.Thread(
        target=_serve_control_handle,
        args=(control_handle, control_state),
        name="kisurf-ai-control",
        daemon=True,
    )
    thread.start()
    return thread


def _serve_control_handle(control_handle: str, control_state: _ControlState) -> None:
    try:
        stream = _open_control_stream(control_handle)
    except Exception:
        return

    with stream:
        while control_state.is_running():
            try:
                payload = _read_frame(stream)
            except Exception:
                return

            if payload is None:
                return

            if not payload:
                continue

            request = session_pb2.WorkerRequest()

            try:
                request.ParseFromString(payload)
            except DecodeError:
                continue

            if request.protocol != PROTOCOL_VERSION:
                continue

            if request.WhichOneof("request") == "cancel_session":
                control_state.cancel_session(str(request.cancel_session.session.id))


def _open_control_stream(control_handle: str):
    if os.name != "nt":
        raise RuntimeError("control handles are only implemented on Windows")

    fd = msvcrt.open_osfhandle(int(control_handle), os.O_RDONLY | os.O_BINARY)
    return os.fdopen(fd, "rb", buffering=0)


def _open_event_stream(event_handle: str):
    if os.name != "nt":
        raise RuntimeError("event handles are only implemented on Windows")

    fd = msvcrt.open_osfhandle(int(event_handle), os.O_WRONLY | os.O_BINARY)
    return os.fdopen(fd, "wb", buffering=0)


def _raise_async_exception(thread_id: int, exception_type: type[BaseException]) -> bool:
    ctypes.pythonapi.PyThreadState_SetAsyncExc.argtypes = [
        ctypes.c_ulong,
        ctypes.py_object,
    ]
    ctypes.pythonapi.PyThreadState_SetAsyncExc.restype = ctypes.c_int

    result = ctypes.pythonapi.PyThreadState_SetAsyncExc(
        ctypes.c_ulong(thread_id),
        ctypes.py_object(exception_type),
    )

    if result == 0:
        return False

    if result > 1:
        ctypes.pythonapi.PyThreadState_SetAsyncExc(ctypes.c_ulong(thread_id), None)
        return False

    return True


def _legacy_run_cell_request(request: session_pb2.RunCellRequest) -> dict[str, object]:
    return {
        "protocol": PROTOCOL_VERSION,
        "type": "run_cell",
        "session": _session_context_to_dict(request.session),
        "cell": {
            "id": request.cell.id,
            "text": request.cell.text,
        },
    }


def _cell_result_response(payload: dict[str, object]) -> session_pb2.WorkerResponse:
    response = session_pb2.WorkerResponse(protocol=PROTOCOL_VERSION)
    result = response.cell_result
    result.ok = bool(payload.get("ok", False))
    result.error_code = str(payload.get("error_code", ""))
    result.message = str(payload.get("message", ""))
    result.stdout_text = str(payload.get("stdout", ""))
    result.stderr_text = str(payload.get("stderr", ""))
    result.step_label = str(payload.get("step_label", ""))
    result.rollback_on_error = bool(payload.get("rollback_on_error", True))

    sdk = payload.get("sdk") if isinstance(payload.get("sdk"), dict) else {}
    result.sdk.name = str(sdk.get("name", ""))
    result.sdk.version = str(sdk.get("version", ""))
    result.sdk.protocol = str(sdk.get("protocol", ""))

    session = payload.get("session") if isinstance(payload.get("session"), dict) else {}
    _dict_to_session_context(result.session, session)

    for event in _list_of_dicts(payload.get("events")):
        event_proto = result.events.add()
        event_proto.kind = str(event.get("kind", ""))
        event_proto.message = str(event.get("message", ""))

        if "payload_json" in event:
            event_proto.payload_json = str(event.get("payload_json", "{}"))
        else:
            event_proto.payload_json = json.dumps(
                event.get("payload", {}),
                separators=(",", ":"),
            )

    for operation in _list_of_dicts(payload.get("operations")):
        operation_proto = result.operations.add()
        operation_proto.kind = _operation_kind_from_id(str(operation.get("kind", "")))

        if "arguments_json" in operation:
            operation_proto.arguments_json = str(operation.get("arguments_json", "{}"))
        else:
            operation_proto.arguments_json = json.dumps(
                operation.get("arguments", {}),
                separators=(",", ":"),
            )

    return response


def _error_response(error_code: str, message: str) -> session_pb2.WorkerResponse:
    response = session_pb2.WorkerResponse(protocol=PROTOCOL_VERSION)
    response.error.error_code = error_code
    response.error.message = message
    response.error.rollback_on_error = True
    return response


def _copy_session_context(
    target: session_pb2.SessionContext,
    source: session_pb2.SessionContext,
) -> None:
    target.id = source.id
    target.board_id = source.board_id
    target.base_hash = source.base_hash
    target.epoch = source.epoch


def _session_context_to_dict(context: session_pb2.SessionContext) -> dict[str, object]:
    return {
        "id": int(context.id),
        "board_id": context.board_id,
        "base_hash": context.base_hash,
        "epoch": int(context.epoch),
    }


def _dict_to_session_context(
    context: session_pb2.SessionContext,
    payload: dict[str, object],
) -> None:
    context.id = _uint64(payload.get("id", 0))
    context.board_id = str(payload.get("board_id", ""))
    context.base_hash = str(payload.get("base_hash", ""))
    context.epoch = _uint64(payload.get("epoch", 0))


def _uint64(value: object) -> int:
    try:
        return max(0, int(value))
    except (TypeError, ValueError):
        return 0


def _list_of_dicts(value: object) -> list[dict[str, object]]:
    if not isinstance(value, list):
        return []

    return [item for item in value if isinstance(item, dict)]


def _operation_kind_from_id(kind_id: str) -> int:
    return _OPERATION_KIND_BY_ID.get(
        kind_id,
        session_pb2.OPERATION_KIND_UNSPECIFIED,
    )


_OPERATION_KIND_BY_ID = {
    "session.checkpoint": session_pb2.SESSION_CHECKPOINT,
    "session.rollback_to": session_pb2.SESSION_ROLLBACK_TO,
    "query.board_summary": session_pb2.QUERY_BOARD_SUMMARY,
    "query.items": session_pb2.QUERY_ITEMS,
    "query.item": session_pb2.QUERY_ITEM,
    "query.selection": session_pb2.QUERY_SELECTION,
    "query.nets": session_pb2.QUERY_NETS,
    "query.layers": session_pb2.QUERY_LAYERS,
    "query.design_rules": session_pb2.QUERY_DESIGN_RULES,
    "query.viewport": session_pb2.QUERY_VIEWPORT,
    "query.activity_timeline": session_pb2.QUERY_ACTIVITY_TIMELINE,
    "render.preview": session_pb2.RENDER_PREVIEW,
    "observe.step": session_pb2.OBSERVE_STEP,
    "pcb.create_via": session_pb2.PCB_CREATE_VIA,
    "pcb.create_track_segment": session_pb2.PCB_CREATE_TRACK_SEGMENT,
    "pcb.create_track_polyline": session_pb2.PCB_CREATE_TRACK_POLYLINE,
    "pcb.create_zone": session_pb2.PCB_CREATE_ZONE,
    "pcb.create_shape": session_pb2.PCB_CREATE_SHAPE,
    "pcb.move_items": session_pb2.PCB_MOVE_ITEMS,
    "pcb.delete_items": session_pb2.PCB_DELETE_ITEMS,
    "pcb.update_item_geometry": session_pb2.PCB_UPDATE_ITEM_GEOMETRY,
    "pcb.set_item_net": session_pb2.PCB_SET_ITEM_NET,
    "pcb.set_item_layer": session_pb2.PCB_SET_ITEM_LAYER,
    "pcb.set_item_properties": session_pb2.PCB_SET_ITEM_PROPERTIES,
    "pcb.set_metadata": session_pb2.PCB_SET_METADATA,
    "pcb.refill_zones": session_pb2.PCB_REFILL_ZONES,
    "pcb.rebuild_connectivity": session_pb2.PCB_REBUILD_CONNECTIVITY,
    "pcb.run_validation": session_pb2.PCB_RUN_VALIDATION,
    "surface.apply_patch": session_pb2.SURFACE_APPLY_PATCH,
}


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
