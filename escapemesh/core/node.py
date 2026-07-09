from typing import List, Optional
from escapemesh.core.routing import GradientRouting, INF
from escapemesh.utils.logger import ColorLogger

class Node:
    """Represents a simulated IoT node (e.g., ESP32)."""
    
    def __init__(self, node_id: str, is_exit: bool = False):
        self.id = node_id
        self.is_exit = is_exit
        self.cost = 0 if is_exit else INF
        self.points_to: Optional['Node'] = None
        self.neighbors: List['Node'] = []
        self.on_fire = False

    def add_neighbor(self, neighbor: 'Node'):
        if neighbor not in self.neighbors:
            self.neighbors.append(neighbor)

    def trigger_fire(self):
        """Simulate local fire detection."""
        self.on_fire = True
        self.cost = INF
        self.points_to = None
        ColorLogger.error(f"Node {self.id} detected FIRE!")

    def tick(self) -> bool:
        """Runs one CPU cycle step for routing logic."""
        new_cost, best_neighbor = GradientRouting.calculate_next_hop(
            self.neighbors, self.is_exit, self.on_fire
        )
        
        if new_cost != self.cost or self.points_to != best_neighbor:
            if self.cost != INF and new_cost > self.cost:
                ColorLogger.warn(f"Node {self.id} rerouting. Cost: {self.cost} -> {new_cost}")
            elif new_cost < self.cost:
                ColorLogger.info(f"Node {self.id} found route. Cost: {self.cost} -> {new_cost}")
                
            self.cost = new_cost
            self.points_to = best_neighbor
            return True
            
        return False
