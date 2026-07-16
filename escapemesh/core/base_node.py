# FILE: escapemesh/core/base_node.py
from __future__ import annotations

from collections import deque
from typing import Deque, Dict, Iterable, List, Optional, Set, Tuple

from escapemesh.utils.logger import ColorLogger

INF = 999


class JacobiQueue(list):
    """Queue that exposes packets only at the beginning of the next tick."""

    def __init__(self):
        super().__init__()
        self.staged = []

    def append(self, item):
        self.staged.append(item)

    def extend(self, items):
        self.staged.extend(items)

    def swap_staged_to_active(self):
        super().clear()
        super().extend(self.staged)
        self.staged.clear()

    def clear(self):
        super().clear()

    def has_pending(self) -> bool:
        """Return True for either currently visible or next-tick packets."""
        return bool(self or self.staged)


class Node:
    """Base IoT node with damped heartbeat-based neighbor liveness."""

    HEARTBEAT_TIMEOUT_TICKS = 9
    RECOVERY_HEARTBEATS = 3
    FLAP_WINDOW_TICKS = 30
    HOLD_DOWN_BASE_TICKS = 6
    HOLD_DOWN_MAX_TICKS = 48

    def __init__(self, node_id: str, is_exit: bool = False):
        self.id = node_id
        self.is_exit = is_exit
        self.cost: float = 0.0 if is_exit else float(INF)
        self.points_to: Optional[Node] = None
        self.neighbors: List[Node] = []
        self.on_fire = False
        self.online = True

        # A disabled link is a physical/simulation condition. Routing protocols do
        # not see it immediately; they see the confirmed heartbeat state instead.
        self._disabled_links: Set[str] = set()

        # Jacobi frozen states.
        self.prev_cost: float = self.cost
        self.prev_points_to: Optional[Node] = self.points_to
        self.prev_on_fire: bool = self.on_fire
        self.route_path: Tuple[str, ...] = (self.id,) if is_exit else tuple()
        self.prev_route_path: Tuple[str, ...] = self.route_path

        # Staged packet queues for synchronous update.
        self.incoming_lsas = JacobiQueue()
        self.incoming_dsdv_updates = JacobiQueue()
        self.incoming_aodv_packets = JacobiQueue()
        self.incoming_dios = JacobiQueue()

        # Heartbeat/link-dampening state.
        self.tick_counter = 0
        self.last_heartbeat_from: Dict[str, int] = {}
        self._neighbor_confirmed_up: Dict[str, bool] = {}
        self._neighbor_recovery_streak: Dict[str, int] = {}
        self._neighbor_hold_down_until: Dict[str, int] = {}
        self._neighbor_failure_ticks: Dict[str, Deque[int]] = {}
        self._neighbor_transitions: List[Tuple[str, bool, int]] = []

    @property
    def is_operational(self) -> bool:
        return self.online and not self.on_fire

    def add_neighbor(self, neighbor: Node):
        if neighbor not in self.neighbors:
            self.neighbors.append(neighbor)
            self._neighbor_confirmed_up.setdefault(neighbor.id, True)
            self._neighbor_recovery_streak.setdefault(neighbor.id, 0)
            self._neighbor_hold_down_until.setdefault(neighbor.id, 0)
            self._neighbor_failure_ticks.setdefault(neighbor.id, deque())

    def trigger_fire(self):
        """Simulate permanent local fire detection."""
        self.on_fire = True
        self.cost = float(INF)
        self.points_to = None
        self.route_path = tuple()
        ColorLogger.error(f"Node {self.id} detected FIRE!")
        self.on_fire_action()

    def on_fire_action(self):
        """Optional lifecycle hook for subclass-specific fire actions."""
        pass

    def set_online(self, online: bool):
        """Temporarily power a node on/off (for overheating/flapping tests)."""
        if self.on_fire or self.online == online:
            return

        self.online = online
        if not online:
            self.cost = float(INF)
            self.points_to = None
            self.route_path = tuple()
            self.on_offline_action()
            ColorLogger.warn(f"Node {self.id} temporarily OFFLINE")
        else:
            # Never resume with a stale route. The protocol must revalidate it.
            self.cost = 0.0 if self.is_exit else float(INF)
            self.points_to = None
            self.route_path = (self.id,) if self.is_exit else tuple()
            self.on_online_action()
            ColorLogger.info(f"Node {self.id} is ONLINE again")

    def on_offline_action(self):
        """Optional hook when a node temporarily powers down."""
        pass

    def on_online_action(self):
        """Optional hook when a node powers back up."""
        pass

    def set_link_enabled(self, neighbor_id: str, enabled: bool):
        """Set one direction of a physical link; MeshNetwork updates both ends."""
        if enabled:
            self._disabled_links.discard(neighbor_id)
        else:
            self._disabled_links.add(neighbor_id)

    def can_transmit_to(self, neighbor: Node) -> bool:
        """Physical delivery check; independent of confirmed routing liveness."""
        return (
            self.is_operational
            and neighbor.is_operational
            and neighbor.id not in self._disabled_links
            and self.id not in neighbor._disabled_links
        )

    def iter_transmittable_neighbors(self) -> Iterable[Node]:
        for neighbor in self.neighbors:
            if self.can_transmit_to(neighbor):
                yield neighbor

    def record_heartbeat(self, sender_id: str, tick: int):
        """Record an arriving keepalive and qualify a previously failed link."""
        if not self.is_operational or sender_id not in self._neighbor_confirmed_up:
            return

        previous_arrival = self.last_heartbeat_from.get(sender_id)
        self.last_heartbeat_from[sender_id] = self.tick_counter

        if self._neighbor_confirmed_up[sender_id]:
            self._neighbor_recovery_streak[sender_id] = 0
            return

        # Require consecutive good heartbeats, not a single lucky packet.
        if previous_arrival is not None and self.tick_counter - previous_arrival <= 1:
            self._neighbor_recovery_streak[sender_id] += 1
        else:
            self._neighbor_recovery_streak[sender_id] = 1

        if (
            self._neighbor_recovery_streak[sender_id] >= self.RECOVERY_HEARTBEATS
            and self.tick_counter >= self._neighbor_hold_down_until[sender_id]
        ):
            self._mark_neighbor_up(sender_id)

    def _mark_neighbor_down(self, neighbor_id: str):
        if not self._neighbor_confirmed_up.get(neighbor_id, False):
            return

        self._neighbor_confirmed_up[neighbor_id] = False
        self._neighbor_recovery_streak[neighbor_id] = 0

        failures = self._neighbor_failure_ticks[neighbor_id]
        while failures and self.tick_counter - failures[0] > self.FLAP_WINDOW_TICKS:
            failures.popleft()
        failures.append(self.tick_counter)

        exponent = max(0, len(failures) - 1)
        hold_down = min(self.HOLD_DOWN_BASE_TICKS * (2**exponent), self.HOLD_DOWN_MAX_TICKS)
        self._neighbor_hold_down_until[neighbor_id] = self.tick_counter + hold_down
        self._neighbor_transitions.append((neighbor_id, False, self.tick_counter))
        ColorLogger.warn(
            f"Node {self.id} suppresses unstable link to {neighbor_id} for {hold_down} ticks"
        )

    def _mark_neighbor_up(self, neighbor_id: str):
        if self._neighbor_confirmed_up.get(neighbor_id, False):
            return
        self._neighbor_confirmed_up[neighbor_id] = True
        self._neighbor_recovery_streak[neighbor_id] = 0
        self._neighbor_transitions.append((neighbor_id, True, self.tick_counter))
        ColorLogger.info(f"Node {self.id} confirms link to {neighbor_id} is stable again")

    def _refresh_neighbor_liveness(self):
        for neighbor in self.neighbors:
            neighbor_id = neighbor.id

            if neighbor.on_fire:
                self._mark_neighbor_down(neighbor_id)
                continue

            last_seen = self.last_heartbeat_from.get(neighbor_id, 0)
            age = self.tick_counter - last_seen

            if self._neighbor_confirmed_up.get(neighbor_id, True):
                if age > self.HEARTBEAT_TIMEOUT_TICKS:
                    self._mark_neighbor_down(neighbor_id)
            elif age > 1:
                # Any gap during qualification restarts the recovery streak.
                self._neighbor_recovery_streak[neighbor_id] = 0

    def consume_neighbor_transitions(self) -> List[Tuple[str, bool, int]]:
        transitions = self._neighbor_transitions[:]
        self._neighbor_transitions.clear()
        return transitions

    def get_link_diagnostics(self, neighbor_id: str) -> Dict[str, int | bool]:
        failures = self._neighbor_failure_ticks.get(neighbor_id, deque())
        return {
            "confirmed_up": self._neighbor_confirmed_up.get(neighbor_id, False),
            "recovery_streak": self._neighbor_recovery_streak.get(neighbor_id, 0),
            "hold_down_until": self._neighbor_hold_down_until.get(neighbor_id, 0),
            "recent_failures": len(failures),
        }

    def pre_tick(self):
        """Freeze state, release staged packets, and emit keepalives."""
        self.tick_counter += 1

        self.prev_cost = self.cost
        self.prev_points_to = self.points_to
        self.prev_on_fire = self.on_fire
        self.prev_route_path = self.route_path

        self.incoming_lsas.swap_staged_to_active()
        self.incoming_dsdv_updates.swap_staged_to_active()
        self.incoming_aodv_packets.swap_staged_to_active()
        self.incoming_dios.swap_staged_to_active()

        if self.is_operational:
            for neighbor in self.iter_transmittable_neighbors():
                neighbor.record_heartbeat(self.id, self.tick_counter)

    def tick(self) -> bool:
        """Run one CPU cycle. Return True when routing state changes."""
        raise NotImplementedError

    def get_active_neighbors(self) -> List[str]:
        """Return only links confirmed up by heartbeat hysteresis."""
        if not self.is_operational:
            return []

        self._refresh_neighbor_liveness()
        return [
            neighbor.id
            for neighbor in self.neighbors
            if not neighbor.on_fire and self._neighbor_confirmed_up.get(neighbor.id, True)
        ]

    def _update_routing_state(self, new_cost: float, best_neighbor: Optional[Node]) -> bool:
        if new_cost >= INF or best_neighbor is None:
            new_cost = 0.0 if self.is_exit else float(INF)
            new_path = (self.id,) if self.is_exit else tuple()
        else:
            parent_path = best_neighbor.prev_route_path or best_neighbor.route_path
            new_path = parent_path + (self.id,) if parent_path else (best_neighbor.id, self.id)

        if new_cost != self.cost or self.points_to != best_neighbor:
            if self.cost != INF and new_cost > self.cost:
                ColorLogger.warn(f"Node {self.id} rerouting. Cost: {self.cost} -> {new_cost}")
            elif new_cost < self.cost:
                ColorLogger.info(f"Node {self.id} found route. Cost: {self.cost} -> {new_cost}")
            elif self.points_to != best_neighbor:
                old_parent = self.points_to.id if self.points_to else None
                new_parent = best_neighbor.id if best_neighbor else None
                ColorLogger.info(f"Node {self.id} switches parent: {old_parent} -> {new_parent}")

            self.cost = new_cost
            self.points_to = best_neighbor
            self.route_path = new_path
            return True

        # Keep path metadata refreshed even if metric/parent did not change.
        self.route_path = new_path
        return False
