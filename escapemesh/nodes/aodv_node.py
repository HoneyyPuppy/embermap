# FILE: escapemesh/nodes/aodv_node.py
from __future__ import annotations

from typing import Dict, List, Optional, Set, Tuple

from escapemesh.core.base_node import INF, Node


class AODVNode(Node):
    """Ad hoc On-Demand Distance Vector routing with damped link failure input."""

    def __init__(self, node_id: str, is_exit: bool = False):
        super().__init__(node_id, is_exit)
        self.aodv_routing_table: Dict[
            str, Tuple[float, int, Optional[str], bool]
        ] = {}
        self.seen_rreqs: Set[Tuple[str, int]] = set()
        self.aodv_rreq_id = 0
        self.rreq_cooldown = 0
        self.sequence_num = 0
        self.last_active_neighbors: List[str] = []
        if is_exit:
            self.aodv_routing_table[self.id] = (
                0.0,
                self.sequence_num,
                None,
                True,
            )

    def on_fire_action(self):
        self.broadcast_aodv_rerr("EXIT", force=True)

    def on_offline_action(self):
        if not self.is_exit:
            self.aodv_routing_table.clear()
        self.seen_rreqs.clear()

    def on_online_action(self):
        self.last_active_neighbors = []
        self.rreq_cooldown = 0
        if self.is_exit:
            self.sequence_num += 2
            self.aodv_routing_table = {
                self.id: (0.0, self.sequence_num, None, True)
            }
        else:
            self.aodv_routing_table.clear()

    def _targets(self, force: bool = False):
        if not force:
            yield from self.iter_transmittable_neighbors()
            return
        for neighbor in self.neighbors:
            if (
                neighbor.is_operational
                and neighbor.id not in self._disabled_links
                and self.id not in neighbor._disabled_links
            ):
                yield neighbor

    def _send_to(self, neighbor_id: str, packet: dict) -> bool:
        for neighbor in self.neighbors:
            if neighbor.id == neighbor_id and self.can_transmit_to(neighbor):
                neighbor.incoming_aodv_packets.append_from(self, packet.copy())
                return True
        return False

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
            "sender": self.id,
        }
        self.seen_rreqs.add((self.id, self.aodv_rreq_id))
        for neighbor in self._targets():
            neighbor.incoming_aodv_packets.append_from(self, rreq.copy())

    def broadcast_aodv_rerr(self, broken_dest: str, force: bool = False):
        rerr = {
            "type": "RERR",
            "broken_dest": broken_dest,
            "seq_num": self.sequence_num,
            "sender": self.id,
        }
        for neighbor in self._targets(force=force):
            neighbor.incoming_aodv_packets.append_from(self, rerr.copy())

    def _sync_best_exit_route(self) -> bool:
        active_set = set(self.get_active_neighbors())
        best_cost = float(INF)
        best_next_hop_id = None
        for destination, (metric, _, next_hop_id, active) in self.aodv_routing_table.items():
            if (
                ("EXIT" in destination or destination.startswith("EX"))
                and active
                and metric < best_cost
                and (next_hop_id is None or next_hop_id in active_set)
            ):
                best_cost = metric
                best_next_hop_id = next_hop_id

        best_neighbor = next(
            (
                neighbor
                for neighbor in self.neighbors
                if neighbor.id == best_next_hop_id
            ),
            None,
        )
        return self._update_routing_state(best_cost, best_neighbor)
>>>>>>> pr-5

    def tick(self) -> bool:
        if not self.is_operational:
            return False

        active_now = self.get_active_neighbors()
        active_set = set(active_now)

        if not self.is_exit:
            has_active_route = any(
                ("EXIT" in destination or destination.startswith("EX"))
                and active
                and (next_hop is None or next_hop in active_set)
                for destination, (_, _, next_hop, active) in self.aodv_routing_table.items()
            )
            if not has_active_route:
                if self.rreq_cooldown == 0:
                    self.broadcast_aodv_rreq()
                    self.rreq_cooldown = 4
                else:
                    self.rreq_cooldown -= 1

        link_broken = False
        lost_nodes = set(self.last_active_neighbors) - active_set
        for lost in lost_nodes:
            for destination in list(self.aodv_routing_table):
                metric, sequence, next_hop_id, active = self.aodv_routing_table[destination]
                if next_hop_id == lost and active:
                    self.aodv_routing_table[destination] = (
                        float(INF),
                        sequence + 1,
                        None,
                        False,
                    )
                    link_broken = True
                    self.broadcast_aodv_rerr(destination)
        self.last_active_neighbors = active_now

        packet_queue = self.incoming_aodv_packets[:]
        self.incoming_aodv_packets.clear()
        state_updated = False

        for packet in packet_queue:
            packet_type = packet["type"]
            sender = packet["sender"]
            if sender not in active_set:
                continue

            if packet_type == "RREQ":
                origin = packet["origin"]
                rreq_id = packet["rreq_id"]
                if (origin, rreq_id) in self.seen_rreqs:
                    continue
                self.seen_rreqs.add((origin, rreq_id))

                self.aodv_routing_table[origin] = (
                    packet["hop_count"] + 1.0,
                    packet["seq_num"],
                    sender,
                    True,
                )

                if self.is_exit:
                    rrep = {
                        "type": "RREP",
                        "origin": origin,
                        "dest": self.id,
                        "seq_num": self.sequence_num,
                        "hop_count": 0.0,
                        "target_node": origin,
                        "sender": self.id,
                    }
                    self._send_to(sender, rrep)
                else:
                    forwarded = packet.copy()
                    forwarded["hop_count"] += 1.0
                    forwarded["sender"] = self.id
                    for neighbor in self._targets():
                        if neighbor.id != sender:
                            neighbor.incoming_aodv_packets.append_from(self, forwarded.copy())

            elif packet_type == "RREP":
                destination = packet["dest"]
                target_node = packet["target_node"]
                received_hops = packet["hop_count"]

                current_metric, current_sequence, _, current_active = (
                    self.aodv_routing_table.get(
                        destination, (float(INF), -1, None, False)
                    )
                )
                if (
                    packet["seq_num"] > current_sequence
                    or (
                        packet["seq_num"] == current_sequence
                        and received_hops + 1.0 < current_metric
                    )
                    or not current_active
                ):
                    self.aodv_routing_table[destination] = (
                        received_hops + 1.0,
                        packet["seq_num"],
                        sender,
                        True,
                    )
                    state_updated = True

                if self.id != target_node:
                    reverse_path = self.aodv_routing_table.get(target_node)
                    if reverse_path and reverse_path[3]:
                        forwarded = packet.copy()
                        forwarded["hop_count"] += 1.0
                        forwarded["sender"] = self.id
                        self._send_to(reverse_path[2], forwarded)

            elif packet_type == "RERR":
                broken_destination = packet["broken_dest"]
                current_metric, current_sequence, current_next_hop, current_active = (
                    self.aodv_routing_table.get(
                        broken_destination, (float(INF), -1, None, False)
                    )
                )
                if current_next_hop == sender and current_active:
                    self.aodv_routing_table[broken_destination] = (
                        float(INF),
                        max(packet["seq_num"], current_sequence + 1),
                        None,
                        False,
                    )
                    state_updated = True
                    forwarded = packet.copy()
                    forwarded["sender"] = self.id
                    for neighbor in self._targets():
                        if neighbor.id != sender:
                            neighbor.incoming_aodv_packets.append_from(self, forwarded.copy())

        route_changed = self._sync_best_exit_route()
        return route_changed or state_updated or link_broken
