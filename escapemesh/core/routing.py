import heapq
from typing import List, Tuple, Optional, Dict, Set

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


class LinkStateRouting:
    """Implements Link-State routing using LSAs flooding and Dijkstra algorithm."""

    @staticmethod
    def run_dijkstra(
        start_node: str, 
        exits: List[str], 
        graph: Dict[str, List[str]]
    ) -> Tuple[int, Optional[str]]:
        """Calculates shortest path cost and next-hop node ID to the nearest exit."""
        if start_node in exits:
            return 0, None
            
        # queue elements: (path_cost, current_node, first_hop_on_path)
        queue = [(0, start_node, None)]
        visited: Set[str] = set()
        
        while queue:
            cost, u, first_hop = heapq.heappop(queue)
            
            if u in visited:
                continue
            visited.add(u)
            
            if u in exits:
                return cost, first_hop
                
            for v in graph.get(u, []):
                if v not in visited:
                    next_first_hop = v if u == start_node else first_hop
                    heapq.heappush(queue, (cost + 1, v, next_first_hop))
                    
        return INF, None
