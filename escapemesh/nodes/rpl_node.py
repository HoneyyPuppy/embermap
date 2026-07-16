# FILE: escapemesh/nodes/rpl_node.py
from __future__ import annotations

from typing import Dict, Optional, Tuple

from escapemesh.core.base_node import INF, Node


class RPLNode(Node):
    """RPL node with parent hysteresis, damped Trickle, and loop rejection."""

    MIN_HOP_RANK_INCREASE = 1.0
    PARENT_SWITCH_THRESHOLD = 1.0
    PARENT_SWITCH_HOLD_DOWN = 6

    TRICKLE_IMIN = 2
    TRICKLE_IMAX = 32
    TRICKLE_REDUNDANCY = 3
    TRICKLE_RESET_COOLDOWN = 6

    def __init__(self, node_id: str, is_exit: bool = False):
        super().__init__(node_id, is_exit)
        self.rank: float = 1.0 if is_exit else float(INF)
        self.cost = self.rank
        self.route_path = (self.id,) if is_exit else tuple()
        self.prev_route_path = self.route_path

        self.preferred_parent: Optional[Node] = None
        self.parent_set: Dict[str, float] = {}
        self.parent_paths: Dict[str, Tuple[str, ...]] = {}
        self.parent_versions: Dict[str, int] = {}
        self.parent_last_heard: Dict[str, int] = {}
        self.last_dio_seq_from: Dict[str, int] = {}

        self.rpl_tick_count = 0
        self.dio_sequence = 0
        self.dodag_version = 0
        self.parent_hold_down_until = 0
        self.parent_switch_count = 0

        self.trickle_interval = self.TRICKLE_IMIN
        self.trickle_elapsed = 0
        self.trickle_tx_at = 1
        self.trickle_sent = False
        self.trickle_consistent_heard = 0
        self.last_trickle_reset_tick = -self.TRICKLE_RESET_COOLDOWN
        self.trickle_reset_count = 0

    def on_fire_action(self):
        self.rank = float(INF)
        self.cost = float(INF)
        self.preferred_parent = None
        self.parent_set.clear()
        self.parent_paths.clear()
        self.route_path = tuple()
        self.broadcast_dio(force=True)

    def on_offline_action(self):
        self.rank = float(INF)
        self.preferred_parent = None
        self.parent_set.clear()
        self.parent_paths.clear()
        self.route_path = tuple()

    def on_online_action(self):
        self.rank = 1.0 if self.is_exit else float(INF)
        self.cost = self.rank
        self.preferred_parent = None
        self.parent_set.clear()
        self.parent_paths.clear()
        self.parent_versions.clear()
        self.parent_last_heard.clear()
        self.last_dio_seq_from.clear()
        self.route_path = (self.id,) if self.is_exit else tuple()
        self._reset_trickle(force=True)

    def _dio_targets(self, force: bool = False):
        if not force:
            yield from self.iter_transmittable_neighbors()
            return

        # A node that just detected fire may emit one final poison DIO.
        for neighbor in self.neighbors:
            if (
                neighbor.is_operational
                and neighbor.id not in self._disabled_links
                and self.id not in neighbor._disabled_links
            ):
                yield neighbor

    def broadcast_dio(self, force: bool = False):
        """Broadcast rank plus an ancestor path used to reject descendant loops."""
        self.dio_sequence += 1
        packet = {
            "sender": self.id,
            "rank": self.rank,
            "version": self.dodag_version,
            "path": tuple(self.route_path),
            "seq": self.dio_sequence,
        }
        for neighbor in self._dio_targets(force=force):
            neighbor.incoming_dios.append(packet.copy())

    def _reset_trickle(self, force: bool = False):
        """Reset at most once per cooldown window to avoid reset storms."""
        can_reset = force or (
            self.rpl_tick_count - self.last_trickle_reset_tick
            >= self.TRICKLE_RESET_COOLDOWN
        )
        if can_reset:
            self.trickle_interval = self.TRICKLE_IMIN
            self.trickle_elapsed = 0
            self.trickle_tx_at = max(1, self.trickle_interval // 2)
            self.trickle_sent = False
            self.trickle_consistent_heard = 0
            self.last_trickle_reset_tick = self.rpl_tick_count
            self.trickle_reset_count += 1
        else:
            # Keep the interval responsive, but do not restart elapsed time.
            self.trickle_interval = min(self.trickle_interval, self.TRICKLE_IMIN)

    def _advance_trickle(self):
        self.trickle_elapsed += 1

        if not self.trickle_sent and self.trickle_elapsed >= self.trickle_tx_at:
            if self.trickle_consistent_heard < self.TRICKLE_REDUNDANCY:
                self.broadcast_dio()
            self.trickle_sent = True

        if self.trickle_elapsed >= self.trickle_interval:
            self.trickle_interval = min(self.trickle_interval * 2, self.TRICKLE_IMAX)
            self.trickle_elapsed = 0
            self.trickle_tx_at = max(1, self.trickle_interval // 2)
            self.trickle_sent = False
            self.trickle_consistent_heard = 0

    @staticmethod
    def _parse_dio(packet):
        # Backward compatibility with the old (sender_id, rank) tuple.
        if isinstance(packet, tuple):
            sender_id, sender_rank = packet[:2]
            return sender_id, sender_rank, 0, tuple(), 0
        return (
            packet["sender"],
            float(packet["rank"]),
            int(packet.get("version", 0)),
            tuple(packet.get("path", tuple())),
            int(packet.get("seq", 0)),
        )

    @staticmethod
    def _path_has_exit(path: Tuple[str, ...]) -> bool:
        return bool(path) and ("EXIT" in path[0] or path[0].startswith("EX"))

    def _remove_parent_candidate(self, parent_id: str) -> bool:
        removed = parent_id in self.parent_set
        self.parent_set.pop(parent_id, None)
        self.parent_paths.pop(parent_id, None)
        self.parent_versions.pop(parent_id, None)
        self.parent_last_heard.pop(parent_id, None)
        return removed

    def _process_dios(self, active_set: set[str]) -> bool:
        topology_changed = False
        current_dios = self.incoming_dios[:]
        self.incoming_dios.clear()

        for raw_packet in current_dios:
            sender_id, sender_rank, version, path, dio_seq = self._parse_dio(raw_packet)
            if sender_id not in active_set:
                continue

            previous_seq = self.last_dio_seq_from.get(sender_id, -1)
            if dio_seq and dio_seq <= previous_seq:
                continue
            if dio_seq:
                self.last_dio_seq_from[sender_id] = dio_seq

            old_rank = self.parent_set.get(sender_id)
            old_path = self.parent_paths.get(sender_id)

            # Poison, malformed root path, or a path containing us means this
            # sender cannot be our parent. The last condition prevents a node
            # from selecting one of its own descendants after an upstream flap.
            invalid = (
                sender_rank >= INF
                or (path and self.id in path)
                or (path and not self._path_has_exit(path))
            )
            if invalid:
                if self._remove_parent_candidate(sender_id):
                    topology_changed = True
                continue

            # Legacy packets do not carry a path. They remain usable, but all
            # new packets generated by this implementation are path-protected.
            if not path:
                path = (sender_id,)

            self.parent_set[sender_id] = sender_rank
            self.parent_paths[sender_id] = path
            self.parent_versions[sender_id] = version
            self.parent_last_heard[sender_id] = self.rpl_tick_count

            if old_rank == sender_rank and old_path == path:
                self.trickle_consistent_heard += 1
            else:
                topology_changed = True

        return topology_changed

    def _select_parent(self) -> bool:
        active_set = set(self.get_active_neighbors())
        candidates = [
            (rank, parent_id)
            for parent_id, rank in self.parent_set.items()
            if parent_id in active_set
            and rank < INF
            and self.id not in self.parent_paths.get(parent_id, tuple())
        ]
        candidates.sort(key=lambda item: (item[0], item[1]))

        current_id = self.preferred_parent.id if self.preferred_parent else None
        current_valid = current_id is not None and any(
            parent_id == current_id for _, parent_id in candidates
        )
        best = candidates[0] if candidates else None

        selected_id: Optional[str] = current_id if current_valid else None
        if selected_id is None and best:
            selected_id = best[1]
        elif selected_id is not None and best and best[1] != selected_id:
            current_rank = self.parent_set[selected_id]
            best_rank = best[0]
            sufficiently_better = (
                best_rank + self.PARENT_SWITCH_THRESHOLD < current_rank
            )
            hold_down_expired = self.rpl_tick_count >= self.parent_hold_down_until
            if sufficiently_better and hold_down_expired:
                selected_id = best[1]

        selected_parent = None
        if selected_id:
            selected_parent = next(
                (neighbor for neighbor in self.neighbors if neighbor.id == selected_id),
                None,
            )

        new_rank = (
            self.parent_set[selected_id] + self.MIN_HOP_RANK_INCREASE
            if selected_parent is not None
            else float(INF)
        )
        new_rank = min(new_rank, float(INF))

        parent_changed = self.preferred_parent is not selected_parent
        state_changed = new_rank != self.rank or parent_changed
        if not state_changed:
            return False

        if parent_changed:
            self.parent_switch_count += 1
            self.parent_hold_down_until = (
                self.rpl_tick_count + self.PARENT_SWITCH_HOLD_DOWN
            )

        self.rank = new_rank
        self.preferred_parent = selected_parent
        changed = self._update_routing_state(new_rank, selected_parent)

        if selected_id and selected_parent:
            self.route_path = self.parent_paths[selected_id] + (self.id,)
        else:
            self.route_path = tuple()

        # A confirmed rank/parent change is inconsistent information. Advertise
        # immediately, then let Trickle back off. Reset is rate-limited.
        self.broadcast_dio()
        self._reset_trickle()
        return changed or state_changed

    def tick(self) -> bool:
        if not self.is_operational:
            return False

        self.rpl_tick_count += 1
        active_set = set(self.get_active_neighbors())

        confirmed_transitions = self.consume_neighbor_transitions()
        topology_changed = bool(confirmed_transitions)

        inactive_parents = [
            parent_id for parent_id in self.parent_set if parent_id not in active_set
        ]
        for parent_id in inactive_parents:
            topology_changed |= self._remove_parent_candidate(parent_id)
            self.last_dio_seq_from.pop(parent_id, None)

        topology_changed |= self._process_dios(active_set)

        if self.is_exit:
            self.rank = 1.0
            self.cost = 1.0
            self.route_path = (self.id,)
            if topology_changed:
                self._reset_trickle()
            self._advance_trickle()
            return False

        state_changed = self._select_parent()
        if topology_changed and not state_changed:
            self._reset_trickle()

        self._advance_trickle()
        return state_changed
