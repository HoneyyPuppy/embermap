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


class DSDVRouting:
    """Implements Destination-Sequenced Distance-Vector updates."""
    
    @staticmethod
    def should_update(
        new_seq: int, new_metric: int, 
        current_seq: int, current_metric: int
    ) -> bool:
        """
        DSDV update rule: 
        1. Higher sequence number is always preferred.
        2. If sequence numbers are equal, the lower metric (cost) is preferred.
        """
        if new_seq > current_seq:
            return True
        elif new_seq == current_seq:
            return new_metric < current_metric
        return False


class AODVRouting:
    """State definition and helper classes for AODV routing packets."""
    pass


class PotentialFieldRouting:
    """Implements Potential Field Routing with attraction and repulsion factors."""

    @staticmethod
    def calculate_potential(
        neighbors: List['Node'], 
        is_exit: bool, 
        on_fire: bool
    ) -> Tuple[float, Optional['Node']]:
        """
        Computes potential and next-hop neighbor.
        Exit potential is 0.0.
        Fire potential is 1000.0 (high repulsion).
        Normal potential = Min(neighbor potentials) + 1.0 + Local Repulsion.
        Repulsion is added if any neighboring node is on fire, pushing routes away from fire zones early.
        """
        if is_exit:
            return 0.0, None
        if on_fire:
            return 1000.0, None

        # Check neighbor fire status to create a local repulsive potential barrier (danger zone warning)
        neighbor_on_fire = any(n.on_fire for n in neighbors)
        repulsion = 5.0 if neighbor_on_fire else 0.0

        best_potential = float(INF)
        best_neighbor = None

        for n in neighbors:
            if n.cost < best_potential:
                best_potential = n.cost
                best_neighbor = n

        new_potential = best_potential + 1.0 + repulsion if best_potential != INF else float(INF)
        # Cap potential at INF to prevent overflow
        if new_potential > INF:
            new_potential = float(INF)
            
        return new_potential, best_neighbor
