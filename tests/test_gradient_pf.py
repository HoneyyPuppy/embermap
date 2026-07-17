import json
import tempfile
import unittest
from pathlib import Path

from escapemesh.core.base_node import INF
from escapemesh.core.network import MeshNetwork

def make_network(protocol: str, nodes: dict, links: list[list[str]]) -> MeshNetwork:
    with tempfile.NamedTemporaryFile("w", suffix=".json", delete=False) as handle:
        json.dump({"nodes": nodes, "links": links}, handle)
        path = handle.name
    try:
        network = MeshNetwork()
        network.load_from_topology(path, protocol)
        return network
    finally:
        Path(path).unlink(missing_ok=True)

class GradientPFTests(unittest.TestCase):
    def test_gradient_convergence(self):
        network = make_network(
            "gradient",
            {
                "EXIT": {"is_exit": True},
                "A": {},
                "B": {},
            },
            [
                ["EXIT", "A"],
                ["A", "B"],
            ],
        )
        # Verify initial state
        self.assertEqual(network.nodes["EXIT"].cost, 0.0)
        self.assertEqual(network.nodes["A"].cost, INF)
        self.assertEqual(network.nodes["B"].cost, INF)

        # Run 1 tick: EXIT broadcasts, A receives and updates its cost next tick
        network.tick()
        # Run another tick: A broadcasts its new cost, B receives and updates next tick
        network.tick()
        network.tick()

        self.assertEqual(network.nodes["A"].cost, 1.0)
        self.assertEqual(network.nodes["A"].points_to.id, "EXIT")
        self.assertEqual(network.nodes["B"].cost, 2.0)
        self.assertEqual(network.nodes["B"].points_to.id, "A")

        # Verify packet transmission counts
        self.assertGreater(network.nodes["EXIT"].routing_message_tx_count, 0)
        self.assertGreater(network.nodes["A"].routing_message_tx_count, 0)

    def test_potential_field_fire_repulsion(self):
        network = make_network(
            "potential_field",
            {
                "EXIT": {"is_exit": True},
                "A": {},
                "B": {},
            },
            [
                ["EXIT", "A"],
                ["A", "B"],
            ],
        )
        # Converge potential field first
        for _ in range(5):
            network.tick()

        self.assertEqual(network.nodes["A"].cost, 1.0)
        self.assertEqual(network.nodes["B"].cost, 2.0)

        # Trigger fire on A
        network.nodes["A"].trigger_fire()
        # Fire should propagate repulsion to neighbor B
        for _ in range(5):
            network.tick()

        # Potential at B should increase due to A being on fire (repulsion of 5.0 added)
        # Since A is on fire, B should route away or have a very high cost
        self.assertGreater(network.nodes["B"].cost, 2.0)
