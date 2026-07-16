# FILE: escapemesh/nodes/potential_field_node.py
from escapemesh.core.base_node import INF, Node


class PotentialFieldNode(Node):
    """Potential-field routing with fire repulsion and loop rejection."""

    def tick(self) -> bool:
        if self.is_exit or not self.is_operational:
            return False
        active_set = set(self.get_active_neighbors())
        neighbor_on_fire = any(neighbor.prev_on_fire for neighbor in self.neighbors if neighbor.id in active_set)
        repulsion = 5.0 if neighbor_on_fire else 0.0

        best_potential = float(INF)
        best_neighbor = None
        for neighbor in self.neighbors:
            if neighbor.id not in active_set:
                continue
            if self.id in neighbor.prev_route_path:
                continue
            if neighbor.prev_cost < best_potential:
                best_potential = neighbor.prev_cost
                best_neighbor = neighbor

        new_potential = (
            best_potential + 1.0 + repulsion
            if best_potential < INF
            else float(INF)
        )
        if new_potential >= INF:
            new_potential = float(INF)
        return self._update_routing_state(new_potential, best_neighbor)
