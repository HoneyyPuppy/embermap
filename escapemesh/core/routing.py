from typing import List, Tuple, Optional

INF = 999

class GradientRouting:
    """Implements Multi-Sink Gradient Field routing algorithm updates."""
    
    @staticmethod
    def calculate_next_hop(
        neighbors: List['Node'], 
        is_exit: bool, 
        on_fire: bool
    ) -> Tuple[int, Optional['Node']]:
        """Calculates path cost and next-hop node based on neighbor costs."""
        if is_exit:
            return 0, None
        if on_fire:
            return INF, None
            
        best_cost = INF
        best_neighbor = None
        
        for n in neighbors:
            if n.cost < best_cost:
                best_cost = n.cost
                best_neighbor = n
                
        new_cost = best_cost + 1 if best_cost != INF else INF
        return new_cost, best_neighbor
