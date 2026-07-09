from typing import List, Optional, Dict, Tuple, Set
from escapemesh.core.routing import GradientRouting, LinkStateRouting, DSDVRouting, PotentialFieldRouting, INF
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
        self.routing_table: Dict[str, Tuple[int, int, Optional[str]]] = {}
        self.incoming_dsdv_updates: List[Tuple[str, Dict[str, Tuple[int, int]]]] = []
        if is_exit:
            self.routing_table[self.id] = (0, self.sequence_num, None)

        # AODV specific state
        # aodv_routing_table: dest_node_id -> (metric, sequence_number, next_hop_node_id, is_active)
        self.aodv_routing_table: Dict[str, Tuple[int, int, Optional[str], bool]] = {}
        self.incoming_aodv_packets: List[Dict] = []
        self.seen_rreqs: Set[Tuple[str, int]] = set()
        self.aodv_rreq_id = 0
        self.rreq_cooldown = 0
        if is_exit:
            self.aodv_routing_table[self.id] = (0, self.sequence_num, None, True)

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
            for dest in list(self.routing_table.keys()):
                m, s, nh = self.routing_table[dest]
                self.routing_table[dest] = (INF, s + 1, None)
            self.broadcast_dsdv_update()
        elif self.routing_mode == "aodv":
            # In AODV, if we catch fire, notify neighbors our paths are broken
            self.broadcast_aodv_rerr("EXIT")

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
        """Processes received LSAs, updates LSDB, and floods changes."""
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
        update_packet = {dest: (m, s) for dest, (m, s, _) in self.routing_table.items()}
        for n in self.neighbors:
            n.incoming_dsdv_updates.append((self.id, update_packet))

    def broadcast_aodv_rreq(self):
        """Initiates an On-Demand Route Request for EXIT."""
        self.aodv_rreq_id += 1
        self.sequence_num += 2
        rreq = {
            "type": "RREQ",
            "origin": self.id,
            "rreq_id": self.aodv_rreq_id,
            "dest": "EXIT",
            "hop_count": 0,
            "seq_num": self.sequence_num,
            "sender": self.id
        }
        self.seen_rreqs.add((self.id, self.aodv_rreq_id))
        for n in self.neighbors:
            if not n.on_fire:
                n.incoming_aodv_packets.append(rreq.copy())

    def broadcast_aodv_rerr(self, broken_dest: str):
        """Sends Route Error to notify neighbors of path loss."""
        rerr = {
            "type": "RERR",
            "broken_dest": broken_dest,
            "seq_num": self.sequence_num,
            "sender": self.id
        }
        for n in self.neighbors:
            if not n.on_fire:
                n.incoming_aodv_packets.append(rerr.copy())

    def tick(self) -> bool:
        """Runs one CPU cycle step for routing logic."""
        if self.routing_mode == "gradient":
            return self._tick_gradient()
        elif self.routing_mode == "link_state":
            return self._tick_link_state()
        elif self.routing_mode == "dsdv":
            return self._tick_dsdv()
        elif self.routing_mode == "aodv":
            return self._tick_aodv()
        elif self.routing_mode == "potential_field":
            return self._tick_potential_field()
        return False

    def _tick_gradient(self) -> bool:
        new_cost, best_neighbor = GradientRouting.calculate_next_hop(
            self.neighbors, self.is_exit, self.on_fire
        )
        return self._update_routing_state(new_cost, best_neighbor)

    def _tick_potential_field(self) -> bool:
        new_potential, best_neighbor = PotentialFieldRouting.calculate_potential(
            self.neighbors, self.is_exit, self.on_fire
        )
        # Using floating potential directly mapped to our cost state variable
        return self._update_routing_state(new_potential, best_neighbor)

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

        if not hasattr(self, 'dsdv_tick_count'):
            self.dsdv_tick_count = 0
        self.dsdv_tick_count += 1

        if self.is_exit:
            if self.sequence_num == 0 or self.dsdv_tick_count % 5 == 0:
                self.sequence_num += 2
                self.routing_table[self.id] = (0, self.sequence_num, None)
                self.broadcast_dsdv_update()
            return False

        link_broken = False
        active_now = self.get_active_neighbors()
        if self.last_active_neighbors and active_now != self.last_active_neighbors:
            lost_nodes = set(self.last_active_neighbors) - set(active_now)
            for lost in lost_nodes:
                for dest in list(self.routing_table.keys()):
                    m, s, nh = self.routing_table[dest]
                    if nh == lost:
                        self.routing_table[dest] = (INF, s + 1, None)
                        link_broken = True
            
        self.last_active_neighbors = active_now

        updates_processed = False
        current_updates = self.incoming_dsdv_updates[:]
        self.incoming_dsdv_updates.clear()

        for neighbor_id, table in current_updates:
            for dest, (metric_rec, seq_rec) in table.items():
                new_metric = metric_rec + 1 if metric_rec != INF else INF
                current_metric, current_seq, current_nh = self.routing_table.get(
                    dest, (INF, -1, None)
                )

                is_broken_route = (seq_rec % 2 != 0)
                should_up = False
                
                if not is_broken_route:
                    should_up = DSDVRouting.should_update(seq_rec, new_metric, current_seq, current_metric)
                else:
                    if current_nh == neighbor_id:
                        should_up = True
                        new_metric = INF

                if should_up:
                    self.routing_table[dest] = (new_metric, seq_rec, neighbor_id if new_metric != INF else None)
                    updates_processed = True

        if updates_processed or link_broken or (not self.routing_table and self.neighbors):
            self.broadcast_dsdv_update()
            
            best_cost = INF
            best_nh_id = None
            for dest, (metric, seq, nh_id) in self.routing_table.items():
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

        if self.dsdv_tick_count % 5 == 0:
            self.broadcast_dsdv_update()

        return False

    def _tick_aodv(self) -> bool:
        if self.on_fire:
            return False


        # 1. On-Demand RREQ triggering
        has_active_route = False
        best_cost = INF
        best_nh_id = None

        for dest, (metric, seq, nh_id, active) in self.aodv_routing_table.items():
            if ("EXIT" in dest or dest.startswith("EX")) and active:
                if metric < best_cost:
                    best_cost = metric
                    best_nh_id = nh_id
                has_active_route = True

        if not has_active_route and not self.is_exit:
            if self.rreq_cooldown == 0:
                self.broadcast_aodv_rreq()
                self.rreq_cooldown = 4  # prevent spamming RREQs
            else:
                self.rreq_cooldown -= 1

        # 2. Local link break detection
        link_broken = False
        active_now = self.get_active_neighbors()
        if self.last_active_neighbors and active_now != self.last_active_neighbors:
            lost_nodes = set(self.last_active_neighbors) - set(active_now)
            for lost in lost_nodes:
                for dest in list(self.aodv_routing_table.keys()):
                    metric, seq, nh_id, active = self.aodv_routing_table[dest]
                    if nh_id == lost and active:
                        # Invalidate route and generate RERR
                        self.aodv_routing_table[dest] = (INF, seq + 1, None, False)
                        link_broken = True
                        self.broadcast_aodv_rerr(dest)
        self.last_active_neighbors = active_now

        # 3. Process incoming packet queues
        packet_queue = self.incoming_aodv_packets[:]
        self.incoming_aodv_packets.clear()
        state_updated = False

        for pkt in packet_queue:
            pkt_type = pkt["type"]
            sender = pkt["sender"]

            if pkt_type == "RREQ":
                origin = pkt["origin"]
                rreq_id = pkt["rreq_id"]
                
                # Deduplicate
                if (origin, rreq_id) in self.seen_rreqs:
                    continue
                self.seen_rreqs.add((origin, rreq_id))

                # Update reverse route back to RREQ origin
                self.aodv_routing_table[origin] = (pkt["hop_count"] + 1, pkt["seq_num"], sender, True)

                # Check if we are destination or have route to target
                if self.is_exit:
                    # Reply with RREP
                    rrep = {
                        "type": "RREP",
                        "origin": origin,
                        "dest": self.id,
                        "seq_num": self.sequence_num,
                        "hop_count": 0,
                        "target_node": origin,
                        "sender": self.id
                    }
                    # Send back via reverse path (sender of RREQ)
                    for n in self.neighbors:
                        if n.id == sender:
                            n.incoming_aodv_packets.append(rrep.copy())
                            break
                else:
                    # Propagate RREQ
                    pkt["hop_count"] += 1
                    pkt["sender"] = self.id
                    for n in self.neighbors:
                        if n.id != sender and not n.on_fire:
                            n.incoming_aodv_packets.append(pkt.copy())

            elif pkt_type == "RREP":
                dest = pkt["dest"]
                target_node = pkt["target_node"]
                rec_hop = pkt["hop_count"]

                # Update forward route to dest
                current_metric, current_seq, current_nh, current_act = self.aodv_routing_table.get(
                    dest, (INF, -1, None, False)
                )
                
                if (pkt["seq_num"] > current_seq) or (pkt["seq_num"] == current_seq and rec_hop + 1 < current_metric) or not current_act:
                    self.aodv_routing_table[dest] = (rec_hop + 1, pkt["seq_num"], sender, True)
                    state_updated = True

                # Forward along the reverse path if we are not the target
                if self.id != target_node:
                    reverse_path = self.aodv_routing_table.get(target_node)
                    if reverse_path and reverse_path[3]:  # if reverse path is active
                        next_hop_to_origin = reverse_path[2]
                        pkt["hop_count"] += 1
                        pkt["sender"] = self.id
                        for n in self.neighbors:
                            if n.id == next_hop_to_origin:
                                n.incoming_aodv_packets.append(pkt.copy())
                                break

            elif pkt_type == "RERR":
                broken_dest = pkt["broken_dest"]
                # Invalidate if our next hop is the node reporting the error
                current_metric, current_seq, current_nh, current_act = self.aodv_routing_table.get(
                    broken_dest, (INF, -1, None, False)
                )
                if current_nh == sender and current_act:
                    self.aodv_routing_table[broken_dest] = (INF, pkt["seq_num"], None, False)
                    state_updated = True
                    # Propagate error
                    pkt["sender"] = self.id
                    for n in self.neighbors:
                        if n.id != sender and not n.on_fire:
                            n.incoming_aodv_packets.append(pkt.copy())

        # Update final cost/hop choices based on routing table changes
        if state_updated or link_broken:
            best_cost = INF
            best_nh_id = None
            for dest, (metric, seq, nh_id, active) in self.aodv_routing_table.items():
                if ("EXIT" in dest or dest.startswith("EX")) and active:
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
