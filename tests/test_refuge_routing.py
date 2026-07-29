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

class RefugeRoutingTests(unittest.TestCase):
    def test_gradient_refuge_failover(self):
        # A node linked to EXIT and REFUGE
        # EXIT - N1 - REFUGE
        network = make_network(
            "gradient",
            {
                "EXIT": {"is_exit": True},
                "REFUGE": {"is_refuge": True},
                "N1": {},
            },
            [
                ["EXIT", "N1"],
                ["N1", "REFUGE"],
            ],
        )
        
        # Verify initial states
        self.assertEqual(network.nodes["EXIT"].exit_cost, 0.0)
        self.assertEqual(network.nodes["REFUGE"].refuge_cost, 0.0)

        # Tick 1: EXIT and REFUGE broadcast, N1 receives both
        network.tick()
        # Tick 2: N1 computes costs and updates routing state
        network.tick()
        network.tick()

        # N1 should prefer EXIT over REFUGE because EXIT is the primary destination
        self.assertEqual(network.nodes["N1"].points_to.id, "EXIT")
        self.assertEqual(network.nodes["N1"].cost, 1.0) # exit_cost is 1.0
        self.assertEqual(network.nodes["N1"].exit_cost, 1.0)
        self.assertEqual(network.nodes["N1"].refuge_cost, 1.0)

        # Cut off the link between EXIT and N1
        network.set_link_state("EXIT", "N1", False)
        
        # Run convergence ticks (at least 12 to trigger heartbeat timeout)
        for _ in range(15):
            network.tick()

        # N1 should now failover to REFUGE because EXIT is cut off
        self.assertEqual(network.nodes["N1"].points_to.id, "REFUGE")
        self.assertEqual(network.nodes["N1"].cost, 1.0) # refuge_cost is 1.0
        self.assertEqual(network.nodes["N1"].exit_cost, INF)
        self.assertEqual(network.nodes["N1"].refuge_cost, 1.0)

    def test_potential_field_refuge_failover(self):
        network = make_network(
            "potential_field",
            {
                "EXIT": {"is_exit": True},
                "REFUGE": {"is_refuge": True},
                "N1": {},
            },
            [
                ["EXIT", "N1"],
                ["N1", "REFUGE"],
            ],
        )
        
        # Run convergence ticks
        for _ in range(15):
            network.tick()

        # N1 should prefer EXIT over REFUGE initially
        self.assertEqual(network.nodes["N1"].points_to.id, "EXIT")
        self.assertEqual(network.nodes["N1"].cost, 1.0)

        # Cut off link to EXIT
        network.set_link_state("EXIT", "N1", False)
        for _ in range(15):
            network.tick()

        # N1 should failover to REFUGE
        self.assertEqual(network.nodes["N1"].points_to.id, "REFUGE")
        self.assertEqual(network.nodes["N1"].cost, 1.0)
        self.assertEqual(network.nodes["N1"].exit_cost, INF)

        # If REFUGE itself catches fire, N1 should have no route (INF cost)
        network.nodes["REFUGE"].trigger_fire()
        for _ in range(15):
            network.tick()

        self.assertEqual(network.nodes["N1"].cost, INF)
        self.assertIsNone(network.nodes["N1"].points_to)
