from typing import List, Dict, Tuple, Set, Optional
from escapemesh.core.base_node import Node, INF

class AODVNode(Node):
    """Node implementing Ad hoc On-Demand Distance Vector routing."""
    def __init__(self, node_id: str, is_exit: bool = False):
        super().__init__(node_id, is_exit)
        self.aodv_routing_table: Dict[str, Tuple[float, int, str, bool]] = {}
        self.seen_rreqs: Set[Tuple[str, int]] = set()
        self.aodv_rreq_id = 0
        self.rreq_cooldown = 0
        self.sequence_num = 0
        self.last_active_neighbors: List[str] = []
        if is_exit:
            self.aodv_routing_table[self.id] = (0.0, self.sequence_num, None, True)

    def on_fire_action(self):
        self.broadcast_aodv_rerr("EXIT")

    def broadcast_aodv_rreq(self):
        self.aodv_rreq_id += 1
        self.sequence_num += 2
        rreq = {
            "type": "RREQ",
            "origin": self.id,
            "rreq_id": self.aodv_rreq_id,
            "dest": "EXIT",
            "hop_count": 0.0,
            "seq_num": self.sequence_num,
            "sender": self.id
        }
        self.seen_rreqs.add((self.id, self.aodv_rreq_id))
        for n in self.neighbors:
            if not n.on_fire:
                n.incoming_aodv_packets.append(rreq.copy())

    def broadcast_aodv_rerr(self, broken_dest: str):
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
        if self.on_fire:
            return False

        if not self.is_exit:
            has_active_route = False
            best_cost = float(INF)
            best_nh_id = None
            for dest, (metric, seq, nh_id, active) in self.aodv_routing_table.items():
                if ("EXIT" in dest or dest.startswith("EX")) and active:
                    if metric < best_cost:
                        best_cost = metric
                        best_nh_id = nh_id
                    has_active_route = True

            if not has_active_route:
                if self.rreq_cooldown == 0:
                    self.broadcast_aodv_rreq()
                    self.rreq_cooldown = 4
                else:
                    self.rreq_cooldown -= 1

        link_broken = False
        active_now = self.get_active_neighbors()
        if self.last_active_neighbors and active_now != self.last_active_neighbors:
            lost_nodes = set(self.last_active_neighbors) - set(active_now)
            for lost in lost_nodes:
                for dest in list(self.aodv_routing_table.keys()):
                    metric, seq, nh_id, active = self.aodv_routing_table[dest]
                    if nh_id == lost and active:
                        self.aodv_routing_table[dest] = (float(INF), seq + 1, None, False)
                        link_broken = True
                        self.broadcast_aodv_rerr(dest)
        self.last_active_neighbors = active_now

        packet_queue = self.incoming_aodv_packets[:]
        self.incoming_aodv_packets.clear()
        state_updated = False

        for pkt in packet_queue:
            pkt_type = pkt["type"]
            sender = pkt["sender"]

            if pkt_type == "RREQ":
                origin = pkt["origin"]
                rreq_id = pkt["rreq_id"]
                if (origin, rreq_id) in self.seen_rreqs:
                    continue
                self.seen_rreqs.add((origin, rreq_id))

                self.aodv_routing_table[origin] = (pkt["hop_count"] + 1.0, pkt["seq_num"], sender, True)

                if self.is_exit:
                    rrep = {
                        "type": "RREP",
                        "origin": origin,
                        "dest": self.id,
                        "seq_num": self.sequence_num,
                        "hop_count": 0.0,
                        "target_node": origin,
                        "sender": self.id
                    }
                    for n in self.neighbors:
                        if n.id == sender:
                            n.incoming_aodv_packets.append(rrep.copy())
                            break
                else:
                    pkt["hop_count"] += 1.0
                    pkt["sender"] = self.id
                    for n in self.neighbors:
                        if n.id != sender and not n.on_fire:
                            n.incoming_aodv_packets.append(pkt.copy())

            elif pkt_type == "RREP":
                dest = pkt["dest"]
                target_node = pkt["target_node"]
                rec_hop = pkt["hop_count"]

                current_metric, current_seq, current_nh, current_act = self.aodv_routing_table.get(
                    dest, (float(INF), -1, None, False)
                )
                
                if (pkt["seq_num"] > current_seq) or (pkt["seq_num"] == current_seq and rec_hop + 1.0 < current_metric) or not current_act:
                    self.aodv_routing_table[dest] = (rec_hop + 1.0, pkt["seq_num"], sender, True)
                    state_updated = True

                if self.id != target_node:
                    reverse_path = self.aodv_routing_table.get(target_node)
                    if reverse_path and reverse_path[3]:
                        next_hop_to_origin = reverse_path[2]
                        pkt["hop_count"] += 1.0
                        pkt["sender"] = self.id
                        for n in self.neighbors:
                            if n.id == next_hop_to_origin:
                                n.incoming_aodv_packets.append(pkt.copy())
                                break

            elif pkt_type == "RERR":
                broken_dest = pkt["broken_dest"]
                current_metric, current_seq, current_nh, current_act = self.aodv_routing_table.get(
                    broken_dest, (float(INF), -1, None, False)
                )
                if current_nh == sender and current_act:
                    self.aodv_routing_table[broken_dest] = (float(INF), pkt["seq_num"], None, False)
                    state_updated = True
                    pkt["sender"] = self.id
                    for n in self.neighbors:
                        if n.id != sender and not n.on_fire:
                            n.incoming_aodv_packets.append(pkt.copy())

        if state_updated or link_broken:
            best_cost = float(INF)
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
