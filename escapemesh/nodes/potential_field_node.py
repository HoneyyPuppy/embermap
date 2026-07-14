from escapemesh.core.base_node import Node, INF

class PotentialFieldNode(Node):
    """Node implementing Potential Field routing with local fire repulsion barriers."""
    def tick(self) -> bool:
        if self.is_exit or self.on_fire:
            return False
            
        # Danger zone repulsion: add +5.0 potential penalty if any neighbor is on fire
        neighbor_on_fire = any(n.prev_on_fire for n in self.neighbors)
        repulsion = 5.0 if neighbor_on_fire else 0.0
        
        best_potential = float(INF)
        best_neighbor = None
        for n in self.neighbors:
            if n.prev_cost < best_potential:
                best_potential = n.prev_cost
                best_neighbor = n
                
        new_potential = best_potential + 1.0 + repulsion if best_potential != INF else float(INF)
        if new_potential > INF:
            new_potential = float(INF)
            
        return self._update_routing_state(new_potential, best_neighbor)
