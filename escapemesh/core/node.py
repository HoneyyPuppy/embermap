from typing import List, Optional, Dict, Tuple
from escapemesh.core.routing import GradientRouting, LinkStateRouting, DSDVRouting, INF
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

        # DSDV specific state
        # routing_table: dest_node_id -> (metric, sequence_number, next_hop_node_id)
        self.routing_table: Dict[str, Tuple[int, int, Optional[str]]] = {}
        self.incoming_dsdv_updates: List[Tuple[str, Dict[str, Tuple[int, int]]]] = []
        if is_exit:
            self.routing_table[self.id] = (0, self.sequence_num, None)

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
            self.broadcast_lsa()
        elif self.routing_mode == "dsdv":
            # In DSDV, when we catch fire, we set all our route metrics to INF and advertise
            for dest in list(self.routing_table.keys()):
                m, s, nh = self.routing_table[dest]
                self.routing_table[dest] = (INF, s + 1, None)
            self.broadcast_dsdv_update()

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

    def broadcast_dsdv_update(self):
        """Broadcasts current routing table metrics and sequence numbers to neighbors."""
        # Update packet maps dest_node_id -> (metric, sequence_number)
        update_packet = {dest: (m, s) for dest, (m, s, _) in self.routing_table.items()}
        for n in self.neighbors:
            n.incoming_dsdv_updates.append((self.id, update_packet))

    def tick(self) -> bool:
        """Runs one CPU cycle step for routing logic."""
        if self.routing_mode == "gradient":
            return self._tick_gradient()
        elif self.routing_mode == "link_state":
            return self._tick_link_state()
        elif self.routing_mode == "dsdv":
            return self._tick_dsdv()
        return False

    def _tick_gradient(self) -> bool:
        new_cost, best_neighbor = GradientRouting.calculate_next_hop(
            self.neighbors, self.is_exit, self.on_fire
        )
        return self._update_routing_state(new_cost, best_neighbor)

    def _tick_link_state(self) -> bool:
        active_now = self.get_active_neighbors()
        if active_now != self.last_active_neighbors:
            self.broadcast_lsa()

        lsdb_changed = self.process_lsas()

        if self.id not in self.lsdb:
            self.broadcast_lsa()
            lsdb_changed = True

        if lsdb_changed and not self.on_fire:
            exits = [node_id for node_id in self.lsdb.keys() if "EXIT" in node_id or node_id.startswith("EX")]
            cost, next_hop_id = LinkStateRouting.run_dijkstra(self.id, exits, self.lsdb)
            
            best_neighbor = None
            if next_hop_id:
                for n in self.neighbors:
                    if n.id == next_hop_id:
                        best_neighbor = n
                        break
            
            return self._update_routing_state(cost, best_neighbor)
            
        return False

    def _tick_dsdv(self) -> bool:
        if self.on_fire:
            return False

        # Track tick counts for periodic DSDV advertisements
        if not hasattr(self, 'dsdv_tick_count'):
            self.dsdv_tick_count = 0
        self.dsdv_tick_count += 1

        # Exits periodic/initial sequence increment
        if self.is_exit:
            # Broadcast initially and then periodically every 5 ticks
            if self.sequence_num == 0 or self.dsdv_tick_count % 5 == 0:
                self.sequence_num += 2
                self.routing_table[self.id] = (0, self.sequence_num, None)
                self.broadcast_dsdv_update()
            return False

        # 1. Detect dead neighbor link changes (triggered update)
        link_broken = False
        active_now = self.get_active_neighbors()
        if self.last_active_neighbors and active_now != self.last_active_neighbors:
            # Find nodes that disappeared
            lost_nodes = set(self.last_active_neighbors) - set(active_now)
            for lost in lost_nodes:
                for dest in list(self.routing_table.keys()):
                    m, s, nh = self.routing_table[dest]
                    if nh == lost:
                        # Mark route as invalid (INF metric, odd sequence number)
                        self.routing_table[dest] = (INF, s + 1, None)
                        link_broken = True
            
        self.last_active_neighbors = active_now

        # 2. Process incoming updates from neighbors
        updates_processed = False
        current_updates = self.incoming_dsdv_updates[:]
        self.incoming_dsdv_updates.clear()

        for neighbor_id, table in current_updates:
            for dest, (metric_rec, seq_rec) in table.items():
                # Metric to destination through this neighbor
                new_metric = metric_rec + 1 if metric_rec != INF else INF
                
                current_metric, current_seq, current_nh = self.routing_table.get(
                    dest, (INF, -1, None)
                )

                is_broken_route = (seq_rec % 2 != 0)
                should_up = False
                
                if not is_broken_route:
                    should_up = DSDVRouting.should_update(seq_rec, new_metric, current_seq, current_metric)
                else:
                    # Invalidate local route only if it depended on the neighbor reporting the break
                    if current_nh == neighbor_id:
                        should_up = True
                        new_metric = INF  # Ensure metric is set to INF for invalid route

                if should_up:
                    self.routing_table[dest] = (new_metric, seq_rec, neighbor_id if new_metric != INF else None)
                    updates_processed = True

        # If we updated our table or detected a link break, broadcast updates
        if updates_processed or link_broken or (not self.routing_table and self.neighbors):
            self.broadcast_dsdv_update()
            
            # Find best exit node cost and next-hop from table
            best_cost = INF
            best_nh_id = None
            
            for dest, (metric, seq, nh_id) in self.routing_table.items():
                # We target nodes that contain "EXIT" or start with "EX"
                if "EXIT" in dest or dest.startswith("EX"):
                    if metric < best_cost:
                        best_cost = metric
                        best_nh_id = nh_id

            best_neighbor = None
            if best_nh_id:
                for n in self.neighbors:
                    if n.id == best_nh_id:
                        best_neighbor = n
                        break
            
            return self._update_routing_state(best_cost, best_neighbor)

        # Periodically broadcast our routing table to ensure convergence of silent nodes
        if self.dsdv_tick_count % 5 == 0:
            self.broadcast_dsdv_update()

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
