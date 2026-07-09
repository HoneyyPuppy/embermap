from typing import List, Optional, Dict, Tuple
from escapemesh.core.routing import GradientRouting, LinkStateRouting, INF
from escapemesh.utils.logger import ColorLogger

class Node:
    """Represents a simulated IoT node (e.g., ESP32)."""
    
    def __init__(self, node_id: str, is_exit: bool = False, routing_mode: str = "gradient"):
        self.id = node_id
        self.is_exit = is_exit
        self.routing_mode = routing_mode
        self.cost = 0 if is_exit else INF
        self.points_to: Optional['Node'] = None
        self.neighbors: List['Node'] = []
        self.on_fire = False

        # Link-State specific state
        self.lsdb: Dict[str, List[str]] = {}
        self.lsa_seqs: Dict[str, int] = {}
        self.sequence_num = 0
        self.incoming_lsas: List[Tuple[str, int, List[str]]] = []
        self.last_active_neighbors: List[str] = []

    def add_neighbor(self, neighbor: 'Node'):
        if neighbor not in self.neighbors:
            self.neighbors.append(neighbor)

    def trigger_fire(self):
        """Simulate local fire detection."""
        self.on_fire = True
        self.cost = INF
        self.points_to = None
        ColorLogger.error(f"Node {self.id} detected FIRE!")
        if self.routing_mode == "link_state":
            # Immediately trigger an LSA update because links are lost
            self.broadcast_lsa()

    def get_active_neighbors(self) -> List[str]:
        """Returns IDs of non-fire neighbors."""
        if self.on_fire:
            return []
        return [n.id for n in self.neighbors if not n.on_fire]

    def broadcast_lsa(self):
        """Generates and sends LSA to all neighbors."""
        self.sequence_num += 1
        active = self.get_active_neighbors()
        self.last_active_neighbors = active
        self.lsdb[self.id] = active
        self.lsa_seqs[self.id] = self.sequence_num
        
        lsa = (self.id, self.sequence_num, active)
        for n in self.neighbors:
            n.incoming_lsas.append(lsa)

    def process_lsas(self) -> bool:
        """Processes received LSAs, updates LSDB, and floods changes. Returns True if LSDB changed."""
        changed = False
        queue_to_flood = []
        
        # Read from incoming LSAs queue
        current_lsas = self.incoming_lsas[:]
        self.incoming_lsas.clear()

        for origin, seq, neighbors in current_lsas:
            # If it's a newer LSA than we have seen
            if seq > self.lsa_seqs.get(origin, -1):
                self.lsa_seqs[origin] = seq
                self.lsdb[origin] = neighbors
                changed = True
                queue_to_flood.append((origin, seq, neighbors))

        # Flood to our neighbors
        for lsa in queue_to_flood:
            for n in self.neighbors:
                # Avoid flooding back to where we got it if we kept track, or just standard flood
                n.incoming_lsas.append(lsa)

        return changed

    def tick(self) -> bool:
        """Runs one CPU cycle step for routing logic."""
        if self.routing_mode == "gradient":
            return self._tick_gradient()
        elif self.routing_mode == "link_state":
            return self._tick_link_state()
        return False

    def _tick_gradient(self) -> bool:
        new_cost, best_neighbor = GradientRouting.calculate_next_hop(
            self.neighbors, self.is_exit, self.on_fire
        )
        return self._update_routing_state(new_cost, best_neighbor)

    def _tick_link_state(self) -> bool:
        # 1. Detect local link changes (e.g. if a neighbor caught fire)
        active_now = self.get_active_neighbors()
        if active_now != self.last_active_neighbors:
            self.broadcast_lsa()

        # 2. Process incoming advertisements
        lsdb_changed = self.process_lsas()

        # If it's the start and we have no database for ourselves, initialize it
        if self.id not in self.lsdb:
            self.broadcast_lsa()
            lsdb_changed = True

        if lsdb_changed and not self.on_fire:
            # Get list of exits in the network (since we need to know exits for Dijkstra)
            # Find exits by inspecting LSDB or exit node configurations
            # In Link-State, the node needs to know which IDs are exit gateways.
            # We assume nodes know exit node names (e.g. any node with "EXIT" in its ID, or explicitly configured)
            exits = [node_id for node_id in self.lsdb.keys() if "EXIT" in node_id or node_id.startswith("EX")]
            
            # Run Dijkstra
            cost, next_hop_id = LinkStateRouting.run_dijkstra(self.id, exits, self.lsdb)
            
            # Resolve next_hop node object
            best_neighbor = None
            if next_hop_id:
                for n in self.neighbors:
                    if n.id == next_hop_id:
                        best_neighbor = n
                        break
            
            return self._update_routing_state(cost, best_neighbor)
            
        return False

    def _update_routing_state(self, new_cost: int, best_neighbor: Optional['Node']) -> bool:
        if new_cost != self.cost or self.points_to != best_neighbor:
            if self.cost != INF and new_cost > self.cost:
                ColorLogger.warn(f"Node {self.id} rerouting. Cost: {self.cost} -> {new_cost}")
            elif new_cost < self.cost:
                ColorLogger.info(f"Node {self.id} found route. Cost: {self.cost} -> {new_cost}")
                
            self.cost = new_cost
            self.points_to = best_neighbor
            return True
        return False
