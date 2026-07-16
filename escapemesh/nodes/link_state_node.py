# FILE: escapemesh/nodes/link_state_node.py
import heapq
from typing import Dict, List, Optional, Set, Tuple

from escapemesh.core.base_node import INF, Node


class LinkStateNode(Node):
    """Link-State routing with damped adjacency changes and LSA flooding."""

    def __init__(self, node_id: str, is_exit: bool = False):
        super().__init__(node_id, is_exit)
        self.lsdb: Dict[str, List[str]] = {}
        self.lsa_seqs: Dict[str, int] = {}
        self.sequence_num = 0
        self.last_active_neighbors: List[str] = []

    def on_fire_action(self):
        self.broadcast_lsa(force=True)

    def broadcast_lsa(self, force: bool = False):
        self.sequence_num += 1
        active = self.get_active_neighbors() if self.is_operational else []
        self.last_active_neighbors = active
        self.lsdb[self.id] = active
        self.lsa_seqs[self.id] = self.sequence_num

        lsa = (self.id, self.sequence_num, active)
        if force:
            targets = [
                neighbor
                for neighbor in self.neighbors
                if neighbor.is_operational
                and neighbor.id not in self._disabled_links
                and self.id not in neighbor._disabled_links
            ]
        else:
            targets = list(self.iter_transmittable_neighbors())
        for neighbor in targets:
            neighbor.incoming_lsas.append(lsa)

    def process_lsas(self) -> bool:
        changed = False
        queue_to_flood = []
        current_lsas = self.incoming_lsas[:]
        self.incoming_lsas.clear()

        for origin, sequence, neighbors in current_lsas:
            if sequence > self.lsa_seqs.get(origin, -1):
                self.lsa_seqs[origin] = sequence
                self.lsdb[origin] = neighbors
                changed = True
                queue_to_flood.append((origin, sequence, neighbors))

        for lsa in queue_to_flood:
            for neighbor in self.iter_transmittable_neighbors():
                neighbor.incoming_lsas.append(lsa)
        return changed

    def tick(self) -> bool:
        if not self.is_operational:
            return False

        active_now = self.get_active_neighbors()
        if active_now != self.last_active_neighbors:
            self.broadcast_lsa()

        lsdb_changed = self.process_lsas()
        if self.id not in self.lsdb:
            self.broadcast_lsa()
            lsdb_changed = True

        if lsdb_changed:
            exits = [
                node_id
                for node_id in self.lsdb
                if "EXIT" in node_id or node_id.startswith("EX")
            ]
            cost, next_hop_id = self.run_dijkstra(exits)
            best_neighbor = next(
                (
                    neighbor
                    for neighbor in self.neighbors
                    if neighbor.id == next_hop_id
                ),
                None,
            )
            return self._update_routing_state(cost, best_neighbor)
        return False

    def run_dijkstra(self, exits: List[str]) -> Tuple[float, Optional[str]]:
        if self.id in exits:
            return 0.0, None

        queue = [(0.0, self.id, None)]
        visited: Set[str] = set()
        while queue:
            cost, node_id, first_hop = heapq.heappop(queue)
            if node_id in visited:
                continue
            visited.add(node_id)
            if node_id in exits:
                return cost, first_hop
            for neighbor_id in self.lsdb.get(node_id, []):
                if neighbor_id not in visited:
                    next_first_hop = neighbor_id if node_id == self.id else first_hop
                    heapq.heappush(
                        queue,
                        (cost + 1.0, neighbor_id, next_first_hop),
                    )
        return float(INF), None
