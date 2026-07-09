from typing import List, Optional
from escapemesh.utils.logger import ColorLogger

INF = 999

class Node:
    """Base IoT node class."""
    def __init__(self, node_id: str, is_exit: bool = False):
        self.id = node_id
        self.is_exit = is_exit
        self.cost: float = 0.0 if is_exit else float(INF)
        self.points_to: Optional['Node'] = None
        self.neighbors: List['Node'] = []
        self.on_fire = False

    def add_neighbor(self, neighbor: 'Node'):
        if neighbor not in self.neighbors:
            self.neighbors.append(neighbor)

    def trigger_fire(self):
        """Simulate local fire detection."""
        self.on_fire = True
        self.cost = float(INF)
        self.points_to = None
        ColorLogger.error(f"Node {self.id} detected FIRE!")
        self.on_fire_action()

    def on_fire_action(self):
        """Optional lifecycle hook for subclass-specific fire actions."""
        pass

    def tick(self) -> bool:
        """Runs one CPU cycle step. Returns True if routing state changed."""
        raise NotImplementedError

    def get_active_neighbors(self) -> List[str]:
        if self.on_fire:
            return []
        return [n.id for n in self.neighbors if not n.on_fire]

    def _update_routing_state(self, new_cost: float, best_neighbor: Optional['Node']) -> bool:
        if new_cost != self.cost or self.points_to != best_neighbor:
            if self.cost != INF and new_cost > self.cost:
                ColorLogger.warn(f"Node {self.id} rerouting. Cost: {self.cost} -> {new_cost}")
            elif new_cost < self.cost:
                ColorLogger.info(f"Node {self.id} found route. Cost: {self.cost} -> {new_cost}")
            self.cost = new_cost
            self.points_to = best_neighbor
            return True
        return False
