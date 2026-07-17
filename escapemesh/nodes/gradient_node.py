# FILE: escapemesh/nodes/gradient_node.py
from escapemesh.core.base_node import INF, Node


class GradientNode(Node):
    """Simple multi-sink gradient with liveness and path-loop rejection."""

    def __init__(self, node_id: str, is_exit: bool = False, packet_loss_rate: float = 0.0):
        super().__init__(node_id, is_exit, packet_loss_rate)
        self.neighbor_states = {}

    def on_online_action(self):
        self.neighbor_states.clear()
        self.broadcast_gradient_update()

    def on_offline_action(self):
        self.neighbor_states.clear()

    def on_fire_action(self):
        self.broadcast_gradient_update()

    def broadcast_gradient_update(self):
        packet = {
            "cost": self.cost,
            "route_path": self.route_path,
        }
        for neighbor in self.iter_transmittable_neighbors():
            neighbor.incoming_gradient_updates.append_from(self, (self.id, packet))

    def tick(self) -> bool:
        if not self.is_operational:
            return False

        # Process incoming updates
        while self.incoming_gradient_updates:
            sender_id, packet = self.incoming_gradient_updates.pop(0)
            self.neighbor_states[sender_id] = packet

        changed = False
        if not self.is_exit:
            active_set = set(self.get_active_neighbors())
            best_cost = float(INF)
            best_neighbor = None
            for neighbor in self.neighbors:
                if neighbor.id not in active_set:
                    continue
                state = self.neighbor_states.get(neighbor.id)
                if not state:
                    continue
                if self.id in state["route_path"]:
                    continue
                if state["cost"] < best_cost:
                    best_cost = state["cost"]
                    best_neighbor = neighbor

            new_cost = best_cost + 1.0 if best_cost < INF else float(INF)
            if new_cost >= INF:
                new_cost = float(INF)
            changed = self._update_routing_state(new_cost, best_neighbor)

        is_periodic = (self.tick_counter % 5 == 0)
        if self.tick_counter == 1 or changed or is_periodic:
            self.broadcast_gradient_update()
        return changed
