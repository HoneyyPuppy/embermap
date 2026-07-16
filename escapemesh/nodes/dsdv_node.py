# FILE: escapemesh/nodes/dsdv_node.py
from __future__ import annotations

from typing import Dict, List, Optional, Tuple

from escapemesh.core.base_node import INF, Node


class DSDVNode(Node):
    """DSDV with strict sequence freshness, poison, and route hold-down."""

    ROUTE_HOLD_DOWN_TICKS = 8
    PERIODIC_UPDATE_TICKS = 5

    def __init__(self, node_id: str, is_exit: bool = False, packet_loss_rate: float = 0.0):
        super().__init__(node_id, is_exit, packet_loss_rate)
        self.routing_table: Dict[str, Tuple[float, int, Optional[str]]] = {}
        self.sequence_num = 0
        self.dsdv_tick_count = 0
        self.last_active_neighbors: List[str] = []
        self.route_hold_down_until: Dict[str, int] = {}
        self.last_broken_next_hop: Dict[str, str] = {}

        if is_exit:
            self.routing_table[self.id] = (0.0, self.sequence_num, None)

    @staticmethod
    def _next_even(sequence: int) -> int:
        return sequence + 2 if sequence % 2 == 0 else sequence + 1

    @staticmethod
    def _next_odd(sequence: int) -> int:
        return sequence + 1 if sequence % 2 == 0 else sequence + 2

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

    def on_fire_action(self):
        for destination in list(self.routing_table):
            metric, sequence, _ = self.routing_table[destination]
            self.routing_table[destination] = (
                float(INF),
                self._next_odd(sequence),
                None,
            )
        self.broadcast_dsdv_update(force=True)

    def on_offline_action(self):
        # Local tables cannot be trusted after a reboot/power cycle.
        if not self.is_exit:
            self.routing_table.clear()

    def on_online_action(self):
        self.last_active_neighbors = []
        self.route_hold_down_until.clear()
        self.last_broken_next_hop.clear()
        if self.is_exit:
            self.sequence_num = self._next_even(self.sequence_num)
            self.routing_table = {self.id: (0.0, self.sequence_num, None)}
        else:
            self.routing_table.clear()

    def broadcast_dsdv_update(self, force: bool = False):
        """Send per-neighbor split-horizon updates."""
        for neighbor in self._targets(force=force):
            update_packet = {}
            for destination, (metric, sequence, next_hop) in self.routing_table.items():
                # Split horizon: never advertise a finite route back to the node
                # from which it was learned. This blocks two-node feedback loops.
                if metric < INF and next_hop == neighbor.id:
                    continue
                update_packet[destination] = (metric, sequence)
            neighbor.incoming_dsdv_updates.append_from(self, (self.id, update_packet))

    def _invalidate_routes_via(self, lost_neighbor: str) -> bool:
        changed = False
        for destination in list(self.routing_table):
            metric, sequence, next_hop = self.routing_table[destination]
            if next_hop != lost_neighbor or metric >= INF:
                continue

            poison_sequence = self._next_odd(sequence)
            self.routing_table[destination] = (float(INF), poison_sequence, None)
            self.route_hold_down_until[destination] = (
                self.dsdv_tick_count + self.ROUTE_HOLD_DOWN_TICKS
            )
            self.last_broken_next_hop[destination] = lost_neighbor
            changed = True
        return changed

    def _process_updates(self, active_set: set[str]) -> bool:
        changed = False
        current_updates = self.incoming_dsdv_updates[:]
        self.incoming_dsdv_updates.clear()

        for neighbor_id, table in current_updates:
            if neighbor_id not in active_set:
                continue

            for destination, (received_metric, received_sequence) in table.items():
                current_metric, current_sequence, current_next_hop = self.routing_table.get(
                    destination, (float(INF), -1, None)
                )

                received_unreachable = (
                    received_metric >= INF or received_sequence % 2 != 0
                )

                if received_unreachable:
                    poison_sequence = (
                        received_sequence
                        if received_sequence % 2 != 0
                        else self._next_odd(received_sequence)
                    )
                    fresher = poison_sequence > current_sequence
                    invalidates_our_next_hop = (
                        current_next_hop == neighbor_id
                        and poison_sequence >= current_sequence
                    )
                    if fresher or invalidates_our_next_hop:
                        self.routing_table[destination] = (
                            float(INF),
                            poison_sequence,
                            None,
                        )
                        self.route_hold_down_until[destination] = (
                            self.dsdv_tick_count + self.ROUTE_HOLD_DOWN_TICKS
                        )
                        self.last_broken_next_hop[destination] = neighbor_id
                        changed = True
                    continue

                # Reachable routes must have an even destination sequence.
                if received_sequence % 2 != 0:
                    continue

                new_metric = received_metric + 1.0
                if new_metric >= INF:
                    continue

                in_hold_down = (
                    self.dsdv_tick_count
                    < self.route_hold_down_until.get(destination, 0)
                )
                fresh_after_failure = received_sequence > current_sequence
                if in_hold_down and not fresh_after_failure:
                    continue

                should_update = False
                if received_sequence > current_sequence:
                    should_update = True
                elif received_sequence == current_sequence:
                    should_update = new_metric < current_metric

                if should_update:
                    self.routing_table[destination] = (
                        new_metric,
                        received_sequence,
                        neighbor_id,
                    )
                    self.route_hold_down_until.pop(destination, None)
                    self.last_broken_next_hop.pop(destination, None)
                    changed = True

        return changed

    def _sync_best_exit_route(self) -> bool:
        active_set = set(self.get_active_neighbors())
        best_cost = float(INF)
        best_next_hop_id = None

        for destination, (metric, sequence, next_hop_id) in self.routing_table.items():
            is_exit = "EXIT" in destination or destination.startswith("EX")
            if (
                is_exit
                and sequence % 2 == 0
                and metric < best_cost
                and (next_hop_id is None or next_hop_id in active_set)
            ):
                best_cost = metric
                best_next_hop_id = next_hop_id

        best_neighbor = None
        if best_next_hop_id:
            best_neighbor = next(
                (
                    neighbor
                    for neighbor in self.neighbors
                    if neighbor.id == best_next_hop_id
                ),
                None,
            )

        return self._update_routing_state(best_cost, best_neighbor)

    def tick(self) -> bool:
        if not self.is_operational:
            return False

        self.dsdv_tick_count += 1
        active_now = self.get_active_neighbors()
        active_set = set(active_now)

        if self.is_exit:
            if self.sequence_num == 0 or self.dsdv_tick_count % self.PERIODIC_UPDATE_TICKS == 0:
                self.sequence_num = self._next_even(self.sequence_num)
                self.routing_table[self.id] = (0.0, self.sequence_num, None)
                self.broadcast_dsdv_update()
            return False

        lost_neighbors = set(self.last_active_neighbors) - active_set
        link_broken = False
        for lost_neighbor in lost_neighbors:
            link_broken = self._invalidate_routes_via(lost_neighbor) or link_broken
        self.last_active_neighbors = active_now

        updates_processed = self._process_updates(active_set)
        table_changed = updates_processed or link_broken

        if table_changed or (not self.routing_table and active_now):
            self.broadcast_dsdv_update()

        if self.dsdv_tick_count % self.PERIODIC_UPDATE_TICKS == 0:
            self.broadcast_dsdv_update()

        route_changed = self._sync_best_exit_route()
        return route_changed or table_changed
