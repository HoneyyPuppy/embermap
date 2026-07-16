# FILE: tests/test_flap_stability.py
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


def run_ticks(network: MeshNetwork, count: int):
    for _ in range(count):
        network.tick()


def assert_no_parent_loop(testcase: unittest.TestCase, network: MeshNetwork):
    for start in network.nodes.values():
        seen = set()
        current = start
        while current is not None:
            testcase.assertNotIn(current.id, seen, f"routing loop starts at {start.id}")
            seen.add(current.id)
            current = current.points_to


class FlapStabilityTests(unittest.TestCase):
    def test_rpl_parent_does_not_ping_pong_after_link_recovers(self):
        # A is the shortest parent. B is a valid but one-hop longer backup.
        network = make_network(
            "rpl",
            {
                "EXIT": {"is_exit": True},
                "A": {},
                "X": {},
                "B": {},
                "C": {},
            },
            [
                ["EXIT", "A"],
                ["EXIT", "X"],
                ["X", "B"],
                ["A", "C"],
                ["B", "C"],
            ],
        )
        run_ticks(network, 50)
        c = network.nodes["C"]
        self.assertEqual(c.points_to.id, "A")

        reset_before = c.trickle_reset_count
        switch_before = c.parent_switch_count

        # Long enough to confirm a failure, then noisy short recoveries.
        network.set_link_state("A", "C", False)
        run_ticks(network, 12)
        self.assertEqual(c.points_to.id, "B")

        for _ in range(3):
            network.set_link_state("A", "C", True)
            run_ticks(network, 4)
            network.set_link_state("A", "C", False)
            run_ticks(network, 4)

        network.set_link_state("A", "C", True)
        run_ticks(network, 30)

        # Hysteresis keeps the working backup instead of ping-ponging back to A.
        self.assertEqual(c.points_to.id, "B")
        self.assertLessEqual(c.parent_switch_count - switch_before, 1)
        self.assertLessEqual(c.trickle_reset_count - reset_before, 6)
        assert_no_parent_loop(self, network)

    def test_rpl_overheating_node_recovery_is_qualified(self):
        network = make_network(
            "rpl",
            {
                "EXIT": {"is_exit": True},
                "A": {},
                "X": {},
                "B": {},
                "C": {},
            },
            [
                ["EXIT", "A"],
                ["EXIT", "X"],
                ["X", "B"],
                ["A", "C"],
                ["B", "C"],
            ],
        )
        run_ticks(network, 50)
        c = network.nodes["C"]
        self.assertEqual(c.points_to.id, "A")

        network.set_node_online("A", False)
        run_ticks(network, 12)
        self.assertEqual(c.points_to.id, "B")

        # Repeated short power-on periods are not enough to make A preferred again.
        for _ in range(3):
            network.set_node_online("A", True)
            run_ticks(network, 2)
            network.set_node_online("A", False)
            run_ticks(network, 3)

        network.set_node_online("A", True)
        run_ticks(network, 35)
        self.assertEqual(c.points_to.id, "B")
        assert_no_parent_loop(self, network)

    def test_rpl_rejects_descendant_and_does_not_count_to_infinity(self):
        network = make_network(
            "rpl",
            {"EXIT": {"is_exit": True}, "A": {}, "B": {}, "C": {}},
            [["EXIT", "A"], ["A", "B"], ["B", "C"]],
        )
        run_ticks(network, 45)
        initial = {node_id: node.cost for node_id, node in network.nodes.items()}
        self.assertEqual(initial["C"], 4.0)

        network.set_link_state("EXIT", "A", False)
        observed = {"A": [], "B": [], "C": []}
        for _ in range(45):
            network.tick()
            for node_id in observed:
                observed[node_id].append(network.nodes[node_id].cost)
            assert_no_parent_loop(self, network)

        for node_id in observed:
            self.assertEqual(network.nodes[node_id].cost, float(INF))
            finite_after_break = [value for value in observed[node_id] if value < INF]
            self.assertTrue(
                all(value <= initial[node_id] for value in finite_after_break),
                f"{node_id} showed count-to-infinity: {finite_after_break}",
            )

    def test_dsdv_poison_and_hold_down_prevent_metric_climb(self):
        network = make_network(
            "dsdv",
            {"EXIT": {"is_exit": True}, "A": {}, "B": {}, "C": {}},
            [["EXIT", "A"], ["A", "B"], ["B", "C"]],
        )
        run_ticks(network, 60)
        initial = {node_id: network.nodes[node_id].cost for node_id in ["A", "B", "C"]}
        self.assertEqual(initial, {"A": 1.0, "B": 2.0, "C": 3.0})

        network.set_link_state("EXIT", "A", False)
        observed = {"A": [], "B": [], "C": []}
        for _ in range(50):
            network.tick()
            for node_id in observed:
                observed[node_id].append(network.nodes[node_id].cost)
            assert_no_parent_loop(self, network)

        for node_id, values in observed.items():
            self.assertEqual(network.nodes[node_id].cost, float(INF))
            finite_values = [value for value in values if value < INF]
            self.assertTrue(
                all(value <= initial[node_id] for value in finite_values),
                f"{node_id} metric climbed: {finite_values}",
            )

        # Re-enable briefly: one/few heartbeats must not resurrect stale routes.
        network.set_link_state("EXIT", "A", True)
        run_ticks(network, 2)
        self.assertEqual(network.nodes["A"].cost, float(INF))


if __name__ == "__main__":
    unittest.main()
