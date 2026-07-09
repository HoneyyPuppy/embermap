import json
import time
from typing import Dict, List, Optional

# // ponytail: zero-dependency colored logger using native ANSI codes
class Log:
    @staticmethod
    def norm(m: str): print(f"\033[92m{m}\033[0m")  # Green
    @staticmethod
    def fire(m: str): print(f"\033[91m{m}\033[0m")  # Red
    @staticmethod
    def warn(m: str): print(f"\033[93m{m}\033[0m")  # Yellow

INF = 999  # Represents unreachable / fire

class Node:
    """Represents an ESP32 node in the mesh."""
    
    def __init__(self, node_id: str, is_exit: bool = False):
        self.id = node_id
        self.is_exit = is_exit
        self.cost = 0 if is_exit else INF
        self.points_to: Optional['Node'] = None
        self.neighbors: List['Node'] = []
        self.on_fire = False

    def add_neighbor(self, neighbor: 'Node'):
        self.neighbors.append(neighbor)

    def trigger_fire(self):
        """Simulates fire detection by a sensor."""
        self.on_fire = True
        self.cost = INF
        self.points_to = None
        Log.fire(f"[FIRE] Node {self.id} detected FIRE! Cost set to INF.")

    def tick(self) -> bool:
        """
        Runs one cycle of the routing algorithm.
        Returns True if this node's state changed (triggering neighbors to update).
        """
        if self.is_exit or self.on_fire:
            return False  # Exits and burning nodes don't change their routing state

        best_cost = INF
        best_neighbor = None

        # Discover lowest cost among neighbors
        for n in self.neighbors:
            if n.cost < best_cost:
                best_cost = n.cost
                best_neighbor = n

        # Our cost is the best neighbor's cost + 1
        new_cost = best_cost + 1 if best_cost != INF else INF

        # Check if state changed
        if new_cost != self.cost or self.points_to != best_neighbor:
            if self.cost != INF and new_cost > self.cost:
                Log.warn(f"[REROUTE] Node {self.id}: Cost {self.cost} -> {new_cost}")
            elif new_cost < self.cost:
                Log.norm(f"[DISCOVER] Node {self.id}: Cost {self.cost} -> {new_cost}")
            
            self.cost = new_cost
            self.points_to = best_neighbor
            return True
            
        return False

class MeshNetwork:
    """Manages the simulated nodes and orchestrates the ticks."""
    
    def __init__(self, topo_file: str):
        self.nodes: Dict[str, Node] = {}
        self._load_topology(topo_file)

    def _load_topology(self, filepath: str):
        """Loads JSON topology and wires up neighbor pointers."""
        with open(filepath, 'r') as f:
            data = json.load(f)
            
        for name, props in data.get("nodes", {}).items():
            self.nodes[name] = Node(name, props.get("is_exit", False))
            
        for n1, n2 in data.get("links", []):
            self.nodes[n1].add_neighbor(self.nodes[n2])
            self.nodes[n2].add_neighbor(self.nodes[n1])

    def tick_all(self):
        """Simulates one broadcast tick across the whole network."""
        changed = False
        # In real life this happens asynchronously. We just loop them here.
        for node in self.nodes.values():
            if node.tick():
                changed = True
        return changed


if __name__ == "__main__":
    print("--- STARTING ESCAPEMESH SIMULATION ---")
    mesh = MeshNetwork("topology.json")
    
    # 1. Initial Discovery (Let the network converge)
    iteration = 1
    while mesh.tick_all():
        print(f"Tick {iteration} complete.")
        iteration += 1
        time.sleep(0.1)
        
    print("\n--- NETWORK STABLE ---")
    for name, node in mesh.nodes.items():
        points = node.points_to.id if node.points_to else "None"
        print(f"Node {name} | Cost: {node.cost} | Points to: {points}")
        
    # 2. Simulate Fire on N2 (Breaking the shortest path for N1)
    print("\n--- SIMULATING FIRE ON N2 ---")
    mesh.nodes["N2"].trigger_fire()
    
    # 3. Network Self-Healing 
    iteration = 1
    while mesh.tick_all():
        print(f"Recovery Tick {iteration} complete.")
        iteration += 1
        time.sleep(0.1)

    print("\n--- RECOVERY COMPLETE ---")
    for name, node in mesh.nodes.items():
        points = node.points_to.id if node.points_to else "None"
        print(f"Node {name} | Cost: {node.cost} | Points to: {points}")
