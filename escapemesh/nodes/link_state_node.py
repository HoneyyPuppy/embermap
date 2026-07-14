import heapq
from typing import List, Tuple, Dict, Set, Optional
from escapemesh.core.base_node import Node, INF

class LinkStateNode(Node):
    """Node implementing Link-State routing with LSA flooding and Dijkstra."""
    def __init__(self, node_id: str, is_exit: bool = False):
        super().__init__(node_id, is_exit)
        self.lsdb: Dict[str, Tuple[int, List[str]]] = {}
        self.lsa_seqs: Dict[str, int] = {}
        self.sequence_num = 0
        self.last_active_neighbors: List[str] = []

    def on_fire_action(self):
        self.broadcast_lsa()

    def broadcast_lsa(self):
        self.sequence_num += 1
        active = self.get_active_neighbors()
        self.last_active_neighbors = active
        self.lsdb[self.id] = active
        self.lsa_seqs[self.id] = self.sequence_num
        
        lsa = (self.id, self.sequence_num, active)
        for n in self.neighbors:
            n.incoming_lsas.append(lsa)

    def process_lsas(self) -> bool:
        changed = False
        queue_to_flood = []
        current_lsas = self.incoming_lsas[:]
        self.incoming_lsas.clear()

        for origin, seq, neighbors in current_lsas:
            if seq > self.lsa_seqs.get(origin, -1):
                self.lsa_seqs[origin] = seq
                self.lsdb[origin] = neighbors
                changed = True
                queue_to_flood.append((origin, seq, neighbors))

        for lsa in queue_to_flood:
            for n in self.neighbors:
                n.incoming_lsas.append(lsa)
        return changed

    def tick(self) -> bool:
        if self.on_fire:
            return False

        active_now = self.get_active_neighbors()
        if active_now != self.last_active_neighbors:
            self.broadcast_lsa()

        lsdb_changed = self.process_lsas()

        if self.id not in self.lsdb:
            self.broadcast_lsa()
            lsdb_changed = True

        if lsdb_changed and not self.on_fire:
            exits = [node_id for node_id in self.lsdb.keys() if "EXIT" in node_id or node_id.startswith("EX")]
            cost, next_hop_id = self.run_dijkstra(exits)
            
            best_neighbor = None
            if next_hop_id:
                for n in self.neighbors:
                    if n.id == next_hop_id:
                        best_neighbor = n
                        break
            return self._update_routing_state(cost, best_neighbor)
        return False

    def run_dijkstra(self, exits: List[str]) -> Tuple[float, Optional[str]]:
        if self.id in exits:
            return 0.0, None
            
        queue = [(0.0, self.id, None)]
        visited: Set[str] = set()
        while queue:
            cost, u, first_hop = heapq.heappop(queue)
            if u in visited:
                continue
            visited.add(u)
            if u in exits:
                return cost, first_hop
            for v in self.lsdb.get(u, []):
                if v not in visited:
                    next_first_hop = v if u == self.id else first_hop
                    heapq.heappush(queue, (cost + 1.0, v, next_first_hop))
        return float(INF), None
