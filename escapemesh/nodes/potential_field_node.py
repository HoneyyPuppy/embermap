# FILE: escapemesh/nodes/potential_field_node.py
from escapemesh.core.base_node import INF, Node


class PotentialFieldNode(Node):
    """Potential-field routing with fire repulsion and loop rejection."""

    def __init__(self, node_id: str, is_exit: bool = False, packet_loss_rate: float = 0.0):
        super().__init__(node_id, is_exit, packet_loss_rate)
        self.neighbor_states = {}

    def on_online_action(self):
        self.neighbor_states.clear()
        self.broadcast_pf_update()

    def on_offline_action(self):
        self.neighbor_states.clear()

    def on_fire_action(self):
        self.broadcast_pf_update()

    def broadcast_pf_update(self):
        packet = {
            "cost": self.cost,
            "route_path": self.route_path,
            "on_fire": self.on_fire,
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
            neighbor_on_fire = False
            for neighbor in self.neighbors:
                if neighbor.id in active_set:
                    state = self.neighbor_states.get(neighbor.id)
                    if state and state.get("on_fire", False):
                        neighbor_on_fire = True
                        break
            repulsion = 5.0 if neighbor_on_fire else 0.0

            best_potential = float(INF)
            best_neighbor = None
            for neighbor in self.neighbors:
                if neighbor.id not in active_set:
                    continue
                state = self.neighbor_states.get(neighbor.id)
                if not state:
                    continue
                if self.id in state["route_path"]:
                    continue
                if state["cost"] < best_potential:
                    best_potential = state["cost"]
                    best_neighbor = neighbor

            new_potential = (
                best_potential + 1.0 + repulsion
                if best_potential < INF
                else float(INF)
            )
            if new_potential >= INF:
                new_potential = float(INF)
            changed = self._update_routing_state(new_potential, best_neighbor)

        is_periodic = (self.tick_counter % 5 == 0)
        if self.tick_counter == 1 or changed or is_periodic:
            self.broadcast_pf_update()
        return changed
