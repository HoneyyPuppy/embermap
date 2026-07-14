from escapemesh.core.base_node import Node, INF

class GradientNode(Node):
    """Node implementing simple Multi-Sink Gradient Field routing."""
    def tick(self) -> bool:
        if self.is_exit or self.on_fire:
            return False
            
        best_cost = float(INF)
        best_neighbor = None
        for n in self.neighbors:
            if n.prev_cost < best_cost:
                best_cost = n.prev_cost
                best_neighbor = n
                
        new_cost = best_cost + 1.0 if best_cost != INF else float(INF)
        return self._update_routing_state(new_cost, best_neighbor)
