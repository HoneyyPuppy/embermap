import json
from typing import Dict
from escapemesh.core.node import Node

class MeshNetwork:
    """Manages collection of nodes and simulation execution."""
    
    def __init__(self):
        self.nodes: Dict[str, Node] = {}

    def load_from_topology(self, filepath: str):
        """Loads and parses JSON topology file."""
        with open(filepath, 'r') as f:
            data = json.load(f)
            
        for name, props in data.get("nodes", {}).items():
            self.nodes[name] = Node(name, props.get("is_exit", False))
            
        for n1, n2 in data.get("links", []):
            if n1 in self.nodes and n2 in self.nodes:
                self.nodes[n1].add_neighbor(self.nodes[n2])
                self.nodes[n2].add_neighbor(self.nodes[n1])

    def tick(self) -> bool:
        """Executes a single synchronous tick across all nodes."""
        changed = False
        for node in list(self.nodes.values()):
            if node.tick():
                changed = True
        return changed
