# FILE: escapemesh/nodes/gradient_node.py
from escapemesh.core.base_node import INF, Node


class GradientNode(Node):
    """Simple multi-sink gradient with liveness and path-loop rejection."""

    def tick(self) -> bool:
        if self.is_exit or not self.is_operational:
            return False

        active_set = set(self.get_active_neighbors())
        best_cost = float(INF)
        best_neighbor = None
        for neighbor in self.neighbors:
            if neighbor.id not in active_set:
                continue
            if self.id in neighbor.prev_route_path:
                continue
            if neighbor.prev_cost < best_cost:
                best_cost = neighbor.prev_cost
                best_neighbor = neighbor

        new_cost = best_cost + 1.0 if best_cost < INF else float(INF)
        if new_cost >= INF:
            new_cost = float(INF)
        return self._update_routing_state(new_cost, best_neighbor)
