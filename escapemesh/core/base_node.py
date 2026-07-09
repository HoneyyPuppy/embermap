from typing import List, Optional, Dict
from escapemesh.utils.logger import ColorLogger

INF = 999

class Node:
    """Base IoT node class with built-in Heartbeat keepalive simulation."""
    def __init__(self, node_id: str, is_exit: bool = False):
        self.id = node_id
        self.is_exit = is_exit
        self.cost: float = 0.0 if is_exit else float(INF)
        self.points_to: Optional['Node'] = None
        self.neighbors: List['Node'] = []
        self.on_fire = False
        
        # Heartbeat tracking state
        self.tick_counter = 0
        self.last_heartbeat_from: Dict[str, int] = {}

    def add_neighbor(self, neighbor: 'Node'):
        if neighbor not in self.neighbors:
            self.neighbors.append(neighbor)

    def trigger_fire(self):
        """Simulate local fire detection. Stops sending heartbeats."""
        self.on_fire = True
        self.cost = float(INF)
        self.points_to = None
        ColorLogger.error(f"Node {self.id} detected FIRE!")
        self.on_fire_action()

    def on_fire_action(self):
        """Optional lifecycle hook for subclass-specific fire actions."""
        pass

    def record_heartbeat(self, sender_id: str, tick: int):
        """Records the arrival of a keepalive heartbeat from a neighbor."""
        self.last_heartbeat_from[sender_id] = tick

    def pre_tick(self):
        """Lifecycle hook run before execution. Increments ticks and broadcasts keepalives."""
        self.tick_counter += 1
        # Broadcast keepalive ping to neighbors every 3 ticks
        if not self.on_fire:
            for n in self.neighbors:
                n.record_heartbeat(self.id, self.tick_counter)

    def tick(self) -> bool:
        """Runs one CPU cycle step. Returns True if routing state changed."""
        raise NotImplementedError

    def get_active_neighbors(self) -> List[str]:
        """Returns neighbor IDs filtering out dead/fire nodes or those who timed out on heartbeats."""
        if self.on_fire:
            return []
        active = []
        for n in self.neighbors:
            if n.on_fire:
                continue
            # Heartbeat timeout: if a neighbor hasn't sent a keepalive in over 9 ticks (3 missed heartbeats)
            if n.id in self.last_heartbeat_from:
                if self.tick_counter - self.last_heartbeat_from[n.id] > 9:
                    continue
            active.append(n.id)
        return active

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
