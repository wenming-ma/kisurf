import json
import pathlib
import sys
import tomllib
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[3]
SDK_ROOT = ROOT / "common" / "kisurf" / "ai" / "python"
sys.path.insert(0, str(SDK_ROOT))

from kisurf_ai.session import KiSurfSession, run_cell  # noqa: E402
from kisurf_ai.worker import handle_worker_request  # noqa: E402
from kisurf_ai import PROTOCOL_VERSION, __version__  # noqa: E402
from kisurf_ai import session_pb2  # noqa: E402


class KiSurfAiSessionSdkTest(unittest.TestCase):
    def test_python_sdk_has_installable_package_metadata(self):
        pyproject_path = SDK_ROOT / "pyproject.toml"

        self.assertTrue(pyproject_path.exists())
        pyproject = tomllib.loads(pyproject_path.read_text(encoding="utf-8"))

        self.assertEqual(pyproject["project"]["name"], "kisurf-ai-session-sdk")
        self.assertEqual(pyproject["project"]["version"], __version__)
        self.assertEqual(pyproject["project"]["requires-python"], ">=3.10")
        self.assertEqual(pyproject["tool"]["setuptools"]["packages"], ["kisurf_ai"])
        self.assertEqual(
            pyproject["project"]["scripts"]["kisurf-ai-worker"],
            "kisurf_ai.worker:main",
        )
        self.assertEqual(PROTOCOL_VERSION, "kisurf.ai.session.v1")

    def test_run_cell_emits_atomic_operations(self):
        request = {
            "protocol": "kisurf.ai.session.v1",
            "type": "run_cell",
            "session": {"id": 42, "board_id": "board-main", "base_hash": "h0", "epoch": 0},
            "cell": {
                "id": "cell-1",
                "text": (
                    "with session.step('place via'):\n"
                    "    session.create_via(position={'x': 1, 'y': 2}, net='GND', alias='v0')\n"
                ),
            },
        }

        response = json.loads(run_cell(json.dumps(request)))

        self.assertTrue(response["ok"])
        self.assertEqual(response["protocol"], "kisurf.ai.session.v1")
        self.assertEqual(response["type"], "cell_result")
        self.assertEqual(response["session"], request["session"])
        self.assertEqual(response["sdk"]["name"], "kisurf-ai-session-sdk")
        self.assertEqual(response["sdk"]["version"], __version__)
        self.assertEqual(response["sdk"]["protocol"], PROTOCOL_VERSION)
        self.assertEqual(response["step_label"], "place via")
        self.assertEqual(len(response["operations"]), 1)
        self.assertEqual(response["operations"][0]["kind"], "pcb.create_via")
        self.assertEqual(response["operations"][0]["arguments"]["alias"], "v0")
        self.assertEqual(response["operations"][0]["arguments"]["net"], "GND")

    def test_run_cell_emits_structured_events(self):
        request = {
            "protocol": "kisurf.ai.session.v1",
            "type": "run_cell",
            "session": {"id": 42, "board_id": "board-main", "base_hash": "h0", "epoch": 0},
            "cell": {
                "id": "cell-events",
                "text": (
                    "session.event('progress', message='placed guard ring', "
                    "payload={'step': 'zone', 'count': 1})\n"
                    "session.event('inspection', message='needs clearance review', "
                    "payload={'severity': 'warning'})\n"
                ),
            },
        }

        response = json.loads(run_cell(json.dumps(request)))

        self.assertTrue(response["ok"])
        self.assertEqual(response["operations"], [])
        self.assertEqual(len(response["events"]), 2)
        self.assertEqual(response["events"][0]["kind"], "progress")
        self.assertEqual(response["events"][0]["message"], "placed guard ring")
        self.assertEqual(response["events"][0]["payload"], {"step": "zone", "count": 1})
        self.assertEqual(response["events"][1]["kind"], "inspection")
        self.assertEqual(response["events"][1]["payload"], {"severity": "warning"})

    def test_via_ring_helper_lowers_to_create_via_ops(self):
        session = KiSurfSession()

        with session.step("via ring"):
            session.create_via_ring(
                center={"x": 10, "y": 20},
                radius=5,
                count=4,
                net="GND",
                alias_prefix="ring",
            )

        result = session.to_result()

        self.assertTrue(result["ok"])
        self.assertEqual(result["step_label"], "via ring")
        self.assertEqual([op["kind"] for op in result["operations"]], ["pcb.create_via"] * 4)
        self.assertEqual([op["arguments"]["alias"] for op in result["operations"]],
                         ["ring_0", "ring_1", "ring_2", "ring_3"])
        self.assertEqual(result["operations"][0]["arguments"]["position"], {"x": 15.0, "y": 20.0})
        self.assertEqual(result["operations"][1]["arguments"]["position"], {"x": 10.0, "y": 25.0})

    def test_annular_zone_helper_lowers_to_create_zone_op(self):
        session = KiSurfSession()

        with session.step("annular copper"):
            session.create_annular_zone(
                center={"x": 100.0, "y": 200.0},
                inner_radius=10.0,
                outer_radius=20.0,
                segments=8,
                layer_set=["F.Cu"],
                net="GND",
                alias="guard-ring",
            )

        result = session.to_result()

        self.assertEqual(len(result["operations"]), 1)
        operation = result["operations"][0]
        self.assertEqual(operation["kind"], "pcb.create_zone")
        self.assertEqual(operation["arguments"]["alias"], "guard-ring")
        self.assertEqual(operation["arguments"]["net"], "GND")
        self.assertEqual(operation["arguments"]["outline"]["type"], "annulus")
        self.assertEqual(len(operation["arguments"]["outline"]["outer"]), 8)
        self.assertEqual(len(operation["arguments"]["outline"]["inner"]), 8)

    def test_stitch_zone_and_fanout_helpers_lower_to_atomic_ops(self):
        session = KiSurfSession()

        with session.step("composite pcb script"):
            session.stitch_zone(
                center={"x": 0.0, "y": 0.0},
                radius=5.0,
                count=3,
                net="GND",
                alias_prefix="stitch",
            )
            session.fanout_pad(
                pad_position={"x": 10.0, "y": 10.0},
                via_position={"x": 20.0, "y": 10.0},
                layer="F.Cu",
                net="/IO1",
                width=0.15,
                via_diameter=0.6,
                via_drill=0.3,
                alias_prefix="u1_io1",
            )

        result = session.to_result()

        self.assertEqual(
            [op["kind"] for op in result["operations"]],
            ["pcb.create_via", "pcb.create_via", "pcb.create_via",
             "pcb.create_track_segment", "pcb.create_via"],
        )
        self.assertEqual(result["operations"][0]["arguments"]["alias"], "stitch_0")
        self.assertEqual(result["operations"][3]["arguments"]["alias"], "u1_io1_escape")
        self.assertEqual(result["operations"][4]["arguments"]["alias"], "u1_io1_via")

    def test_observation_helpers_emit_non_mutating_operations(self):
        session = KiSurfSession()

        with session.step("inspect after placement"):
            session.query_board_summary()
            session.query_items(filter={"type": "via", "net": "GND"})
            session.observe_step(step_id=3)
            session.render_preview(
                region={"x": 0, "y": 0, "width": 10, "height": 10},
                layer_mask=["F.Cu", "B.Cu"],
                mode="native",
            )

        result = session.to_result()

        self.assertEqual(
            [op["kind"] for op in result["operations"]],
            ["query.board_summary", "query.items", "observe.step", "render.preview"],
        )
        self.assertEqual(result["operations"][1]["arguments"]["filter"]["type"], "via")
        self.assertEqual(result["operations"][2]["arguments"]["step_id"], 3)
        self.assertEqual(result["operations"][3]["arguments"]["mode"], "native")
        self.assertEqual(result["operations"][3]["arguments"]["layer_mask"], ["F.Cu", "B.Cu"])

    def test_mutation_maintenance_helpers_emit_atomic_operations(self):
        session = KiSurfSession()

        with session.step("edit primitives"):
            session.move_items(handles=["v0"], delta={"x": 10, "y": -5})
            session.move_items(handles=["v0"], target_positions={"v0": {"x": 100, "y": 200}})
            session.update_item_geometry(handle="v0", geometry_patch={"diameter": 600000})
            session.set_item_net(handle="v0", net="GND")
            session.set_item_layer(handle="zone0", layer="B.Cu")
            session.set_item_properties(handle="track0", typed_props={"width": 125000})
            session.set_metadata(handle="v0", key_values={"role": "stitch"})
            session.delete_items(handles=["temp0"])
            session.refill_zones(handles=["zone0"])
            session.rebuild_connectivity(scope={"handles": ["v0", "track0"]})
            session.run_validation(scope="session", level="geometry")

        result = session.to_result()

        self.assertEqual(
            [op["kind"] for op in result["operations"]],
            [
                "pcb.move_items",
                "pcb.move_items",
                "pcb.update_item_geometry",
                "pcb.set_item_net",
                "pcb.set_item_layer",
                "pcb.set_item_properties",
                "pcb.set_metadata",
                "pcb.delete_items",
                "pcb.refill_zones",
                "pcb.rebuild_connectivity",
                "pcb.run_validation",
            ],
        )
        self.assertEqual(result["operations"][1]["arguments"]["target_positions"]["v0"],
                         {"x": 100, "y": 200})
        self.assertEqual(result["operations"][5]["arguments"]["typed_props"]["width"], 125000)
        self.assertEqual(result["operations"][8]["arguments"]["handles"], ["zone0"])
        self.assertEqual(result["operations"][10]["arguments"]["level"], "geometry")

    def test_set_item_layer_can_emit_layer_set(self):
        session = KiSurfSession()

        with session.step("make zone multilayer"):
            session.set_item_layer(handle="zone0", layer_set=["F.Cu", "B.Cu"])

        result = session.to_result()

        self.assertEqual(result["operations"][0]["kind"], "pcb.set_item_layer")
        self.assertEqual(result["operations"][0]["arguments"]["handle"], "zone0")
        self.assertEqual(result["operations"][0]["arguments"]["layer_set"],
                         ["F.Cu", "B.Cu"])
        self.assertNotIn("layer", result["operations"][0]["arguments"])

    def test_control_helpers_emit_checkpoint_and_rollback_operations(self):
        session = KiSurfSession()

        session.checkpoint("before clearance trial")
        session.rollback_to(7)

        result = session.to_result()

        self.assertEqual(
            [op["kind"] for op in result["operations"]],
            ["session.checkpoint", "session.rollback_to"],
        )
        self.assertEqual(result["operations"][0]["arguments"]["name"],
                         "before clearance trial")
        self.assertEqual(result["operations"][1]["arguments"]["checkpoint_id"], 7)

    def test_run_cell_exception_returns_failed_result_without_operations(self):
        request = {
            "protocol": "kisurf.ai.session.v1",
            "type": "run_cell",
            "session": {"id": 42, "board_id": "board-main", "base_hash": "h0", "epoch": 0},
            "cell": {"id": "bad", "text": "raise RuntimeError('spacing failed')"},
        }

        response = json.loads(run_cell(json.dumps(request)))

        self.assertFalse(response["ok"])
        self.assertEqual(response["protocol"], "kisurf.ai.session.v1")
        self.assertEqual(response["type"], "cell_result")
        self.assertEqual(response["session"], request["session"])
        self.assertEqual(response["error_code"], "python_exception")
        self.assertIn("spacing failed", response["message"])
        self.assertEqual(response["operations"], [])
        self.assertTrue(response["rollback_on_error"])

    def test_run_cell_reuses_namespace_when_supplied(self):
        namespace = {}
        first = {
            "protocol": "kisurf.ai.session.v1",
            "type": "run_cell",
            "session": {"id": 42, "board_id": "board-main", "base_hash": "h0", "epoch": 0},
            "cell": {"id": "define", "text": "pitch = 7"},
        }
        run_cell(json.dumps(first), namespace)

        second = {
            "protocol": "kisurf.ai.session.v1",
            "type": "run_cell",
            "session": {"id": 42, "board_id": "board-main", "base_hash": "h0", "epoch": 0},
            "cell": {
                "id": "use",
                "text": (
                    "with session.step('reuse namespace'):\n"
                    "    session.create_via(position={'x': pitch, 'y': pitch + 1})\n"
                ),
            },
        }

        response = json.loads(run_cell(json.dumps(second), namespace))

        self.assertTrue(response["ok"])
        self.assertEqual(response["step_label"], "reuse namespace")
        self.assertEqual(response["operations"][0]["arguments"]["position"]["x"], 7)
        self.assertEqual(response["operations"][0]["arguments"]["position"]["y"], 8)

    def test_worker_cancel_session_clears_namespace(self):
        namespaces = {"42": {"pitch": 7}}
        request = session_pb2.WorkerRequest(protocol="kisurf.ai.session.v1")
        request.cancel_session.session.id = 42
        request.cancel_session.reason = "user rejected preview"

        response, should_exit = handle_worker_request(
            request,
            namespaces,
        )

        self.assertFalse(should_exit)
        self.assertEqual(response.protocol, "kisurf.ai.session.v1")
        self.assertTrue(response.HasField("cancel_session_ack"))
        self.assertEqual(response.cancel_session_ack.session.id, 42)
        self.assertEqual(response.cancel_session_ack.reason, "user rejected preview")
        self.assertNotIn("42", namespaces)


if __name__ == "__main__":
    unittest.main()
