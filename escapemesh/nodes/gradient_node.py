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
        self.exit_cost = float(INF)
        self.refuge_cost = float(INF)
        self.exit_path = tuple()
        self.refuge_path = tuple()
        self.broadcast_gradient_update()

    def broadcast_gradient_update(self):
        packet = {
            "exit_cost": self.exit_cost,
            "exit_path": self.exit_path,
            "refuge_cost": self.refuge_cost,
            "refuge_path": self.refuge_path,
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
            
            # 1. Update Exit Path
            best_exit_cost = float(INF)
            best_exit_neighbor = None
            for neighbor in self.neighbors:
                if neighbor.id not in active_set:
                    continue
                state = self.neighbor_states.get(neighbor.id)
                if not state:
                    continue
                if self.id in state.get("exit_path", tuple()):
                    continue
                c = state.get("exit_cost", float(INF))
                if c < best_exit_cost:
                    best_exit_cost = c
                    best_exit_neighbor = neighbor

            new_exit_cost = best_exit_cost + 1.0 if best_exit_cost < INF else float(INF)
            if new_exit_cost >= INF:
                new_exit_cost = float(INF)
                new_exit_path = tuple()
            else:
                p_path = self.neighbor_states[best_exit_neighbor.id].get("exit_path", tuple())
                new_exit_path = p_path + (self.id,) if p_path else (best_exit_neighbor.id, self.id)

            # 2. Update Refuge Path
            if self.is_refuge:
                new_refuge_cost = 0.0
                new_refuge_path = (self.id,)
            else:
                best_refuge_cost = float(INF)
                best_refuge_neighbor = None
                for neighbor in self.neighbors:
                    if neighbor.id not in active_set:
                        continue
                    state = self.neighbor_states.get(neighbor.id)
                    if not state:
                        continue
                    if self.id in state.get("refuge_path", tuple()):
                        continue
                    c = state.get("refuge_cost", float(INF))
                    if c < best_refuge_cost:
                        best_refuge_cost = c
                        best_refuge_neighbor = neighbor

                new_refuge_cost = best_refuge_cost + 1.0 if best_refuge_cost < INF else float(INF)
                if new_refuge_cost >= INF:
                    new_refuge_cost = float(INF)
                    new_refuge_path = tuple()
                else:
                    p_path = self.neighbor_states[best_refuge_neighbor.id].get("refuge_path", tuple())
                    new_refuge_path = p_path + (self.id,) if p_path else (best_refuge_neighbor.id, self.id)

            # 3. Failover/active selection
            if new_exit_cost < INF:
                active_cost = new_exit_cost
                active_points_to = best_exit_neighbor
                active_path = new_exit_path
            elif self.is_refuge:
                active_cost = 0.0
                active_points_to = None
                active_path = (self.id,)
            elif new_refuge_cost < INF:
                active_cost = new_refuge_cost
                active_points_to = best_refuge_neighbor
                active_path = new_refuge_path
            else:
                active_cost = float(INF)
                active_points_to = None
                active_path = tuple()

            state_changed = (
                new_exit_cost != self.exit_cost or
                new_refuge_cost != self.refuge_cost or
                active_cost != self.cost or
                active_points_to != self.points_to
            )
            
            if state_changed:
                self.exit_cost = new_exit_cost
                self.exit_path = new_exit_path
                self.refuge_cost = new_refuge_cost
                self.refuge_path = new_refuge_path
                self.cost = active_cost
                self.points_to = active_points_to
                self.route_path = active_path
                changed = True

        is_periodic = (self.tick_counter % 5 == 0)
        if self.tick_counter == 1 or changed or is_periodic:
            self.broadcast_gradient_update()
        return changed
