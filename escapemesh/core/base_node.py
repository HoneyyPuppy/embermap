from __future__ import annotations
import random
from collections import deque
from typing import Deque, Dict, Iterable, List, Optional, Set, Tuple
from escapemesh.utils.logger import ColorLogger

INF = 999


class JacobiQueue(list):
    """List subclass that stages appends during a tick and releases them at their scheduled delivery tick."""
    def __init__(self, receiver: 'Node' = None):
        super().__init__()
        self.staged = []  # Contains tuples of (delivery_tick, sender_id, item)
        self.receiver = receiver

    def append(self, item):
        # Fallback to default staging (instant next-tick delivery) if sender is unknown
        current_tick = getattr(self.receiver, 'tick_counter', 0)
        self.staged.append((current_tick + 1, None, item))

    def append_from(self, sender: 'Node', item):
        if not self.receiver:
            self.staged.append((1, None, item))
            return

        current_tick = getattr(self.receiver, 'tick_counter', 0)
        sender.is_transmitting = True
        sender.routing_message_tx_count = getattr(sender, 'routing_message_tx_count', 0) + 1

        # If physical simulation is disabled (ideal simulation mode)
        physical_enabled = getattr(self.receiver, 'physical_simulation_enabled', True) if self.receiver else True
        if not physical_enabled:
            per = getattr(self.receiver, 'packet_loss_rate', 0.0)
            if random.random() < per:
                return  # Packet dropped!
            self.staged.append((current_tick + 1, sender.id, item))
            return

        # 1. Calculate physical 3D distance
        dx = sender.x - self.receiver.x
        dy = sender.y - self.receiver.y
        dz = (getattr(sender, 'floor', 0) - getattr(self.receiver, 'floor', 0)) * 100
        d = (dx**2 + dy**2 + dz**2)**0.5

        # 2. Calculate distance-based loss (perfect under 50px, completely lost above receiver's max_distance)
        d_min = 50.0
        d_max = getattr(self.receiver, 'max_distance', 300.0)
        if d <= d_min:
            per_dist = 0.0
        elif d >= d_max:
            per_dist = 1.0
        else:
            per_dist = (d - d_min) / (d_max - d_min) if d_max > d_min else 0.0

        # 3. Calculate fire-attenuation loss (smoke/heat blocking signal)
        sender_near_fire = any(n.prev_on_fire for n in sender.neighbors)
        receiver_near_fire = any(n.prev_on_fire for n in self.receiver.neighbors)
        fire_penalty_val = getattr(self.receiver, 'fire_penalty', 0.4)
        fire_penalty = fire_penalty_val if (sender_near_fire or receiver_near_fire) else 0.0

        # Combined loss (Distance PER + Fire Attenuation + Baseline Packet Loss Rate)
        per = min(1.0, per_dist + fire_penalty + getattr(self.receiver, 'packet_loss_rate', 0.0))

        if random.random() < per:
            return  # Packet dropped!

        # 4. Calculate Propagation & Queue Occupancy Delay
        delay_factor = getattr(self.receiver, 'delay_factor', 0.0)
        jitter_ticks = getattr(self.receiver, 'jitter_ticks', 0)

        # Distance Delay (ticks per 100px)
        d_dist = int(d * delay_factor / 100)

        # Random Jitter (randomly selected from [1, D_max] if D_max > 0, otherwise baseline of 1 tick)
        d_jitter = random.randint(1, jitter_ticks) if jitter_ticks > 0 else 1

        # Queue Occupancy Delay (+1 tick for every 5 backlogged packets in receiver's active queues)
        queue_occupancy = (
            len(getattr(self.receiver, 'incoming_lsas', [])) +
            len(getattr(self.receiver, 'incoming_dsdv_updates', [])) +
            len(getattr(self.receiver, 'incoming_aodv_packets', [])) +
            len(getattr(self.receiver, 'incoming_dios', []))
        )
        d_queue = int(queue_occupancy / 5)

        total_delay = d_dist + d_jitter + d_queue

        # 5. CSMA/CA Carrier Sense & Backoff
        csma_enabled = getattr(self.receiver, 'csma_enabled', True)
        if csma_enabled:
            # Sense the channel: check if any active neighbor of the sender is currently transmitting
            channel_busy = any(n.is_transmitting for n in sender.neighbors if not n.prev_on_fire)
            if channel_busy:
                # Channel is busy! Back off by adding a random slot delay (2-5 ticks)
                total_delay += random.randint(2, 5)

        delivery_tick = current_tick + total_delay
        self.staged.append((delivery_tick, sender.id, item))

    def extend(self, items):
        # Treat as standard staged appends with instant next-tick delivery
        current_tick = getattr(self.receiver, 'tick_counter', 0)
        for item in items:
            self.staged.append((current_tick + 1, None, item))

    def swap_staged_to_active(self, current_tick: int):
        super().clear()
        
        # If physical simulation is disabled, deliver directly without collision checks
        physical_enabled = getattr(self.receiver, 'physical_simulation_enabled', True) if self.receiver else True
        if not physical_enabled:
            undelivered = []
            for delivery_tick, sender_id, item in self.staged:
                if delivery_tick <= current_tick:
                    super().append(item)
                else:
                    undelivered.append((delivery_tick, sender_id, item))
            self.staged = undelivered
            return

        # 1. Group expired packets by their scheduled delivery_tick
        expired_by_tick = {}
        undelivered = []
        for delivery_tick, sender_id, item in self.staged:
            if delivery_tick <= current_tick:
                expired_by_tick.setdefault(delivery_tick, []).append((sender_id, item))
            else:
                undelivered.append((delivery_tick, sender_id, item))
        
        # 2. Check for collisions at each tick (only if CSMA/CA is enabled)
        csma_enabled = getattr(self.receiver, 'csma_enabled', True) if self.receiver else True
        for tick_val, packets in expired_by_tick.items():
            # If multiple packets from DIFFERENT senders arrive at the exact same tick, they collide!
            unique_senders = set(sender_id for sender_id, _ in packets if sender_id is not None)
            if csma_enabled and len(unique_senders) > 1:
                # Collision detected! Drop all packets at this tick
                sender_names = ", ".join(unique_senders)
                ColorLogger.warn(f"[COLLISION] Packets from ({sender_names}) collided at {self.receiver.id} at tick {tick_val}!")
                continue
            
            # Non-colliding packets are delivered to the active queue
            for _, item in packets:
                super().append(item)
                
        self.staged = undelivered

    def clear(self):
        super().clear()

    def has_pending(self) -> bool:
        """Return True for either currently visible or next-tick packets."""
        return bool(self or self.staged)


class Node:
    """Base IoT node class with built-in Heartbeat keepalive simulation and flapping stability."""

    HEARTBEAT_TIMEOUT_TICKS = 9
    RECOVERY_HEARTBEATS = 3
    FLAP_WINDOW_TICKS = 30
    HOLD_DOWN_BASE_TICKS = 6
    HOLD_DOWN_MAX_TICKS = 48

    def __init__(self, node_id: str, is_exit: bool = False, packet_loss_rate: float = 0.0):
        self.id = node_id
        self.is_exit = is_exit
        self.cost: float = 0.0 if is_exit else float(INF)
        self.points_to: Optional[Node] = None
        self.neighbors: List[Node] = []
        self.on_fire = False
        self.packet_loss_rate = packet_loss_rate
        
        # Physical coordinates & delay settings
        self.x: float = 0.0
        self.y: float = 0.0
        self.floor: int = 0
        self.delay_factor: float = 0.0
        self.jitter_ticks: int = 0
        self.csma_enabled: bool = True
        self.is_transmitting: bool = False
        self.routing_message_tx_count: int = 0
        
        # MQ2 Sensor settings
        self.smoke_level: float = 100.0
        self.smoke_threshold: float = 400.0
        self.smoke_propagation_enabled: bool = True
        self.smoke_increment: float = 40.0
        self.physical_simulation_enabled: bool = True
        
        self.online = True

        # A disabled link is a physical/simulation condition. Routing protocols do
        # not see it immediately; they see the confirmed heartbeat state instead.
        self._disabled_links: Set[str] = set()
        self.prev_cost: float = self.cost
        self.prev_points_to: Optional[Node] = self.points_to
        self.prev_on_fire: bool = self.on_fire
        self.route_path: Tuple[str, ...] = (self.id,) if is_exit else tuple()
        self.prev_route_path: Tuple[str, ...] = self.route_path

        # Staged packet queues for synchronous update with packet loss check
        self.incoming_lsas = JacobiQueue(self)
        self.incoming_dsdv_updates = JacobiQueue(self)
        self.incoming_aodv_packets = JacobiQueue(self)
        self.incoming_dios = JacobiQueue(self)
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
        self.smoke_level = max(self.smoke_level, 1000.0) # Ensure full smoke at fire site
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
        self.is_transmitting = False
        
        # Freeze cost state for Jacobi synchronous update
        self.prev_cost = self.cost
        self.prev_points_to = self.points_to
        self.prev_on_fire = self.on_fire
        self.prev_route_path = self.route_path

        # Smoke diffusion logic
        if getattr(self, 'smoke_propagation_enabled', True) and not self.on_fire and not self.is_exit:
            smoke_increase = 0.0
            for n in self.neighbors:
                if n.prev_on_fire:
                    smoke_increase += getattr(self, 'smoke_increment', 40.0) # PPM per adjacent fire source per tick
            if smoke_increase > 0:
                self.smoke_level = min(1000.0, self.smoke_level + smoke_increase)
                if self.smoke_level >= self.smoke_threshold:
                    self.trigger_fire()
                    # Re-freeze since trigger_fire alters cost/state
                    self.prev_cost = self.cost
                    self.prev_points_to = self.points_to
                    self.prev_on_fire = self.on_fire
                    self.prev_route_path = self.route_path
        
        # Release staged packets for Jacobi synchronous message passing (based on delivery schedule)
        self.incoming_lsas.swap_staged_to_active(self.tick_counter)
        self.incoming_dsdv_updates.swap_staged_to_active(self.tick_counter)
        self.incoming_aodv_packets.swap_staged_to_active(self.tick_counter)
        self.incoming_dios.swap_staged_to_active(self.tick_counter)
        
        # Broadcast keepalive ping to transmittable neighbors with physical distance loss checks
        if self.is_operational:
            for n in self.iter_transmittable_neighbors():
                physical_enabled = getattr(self, 'physical_simulation_enabled', True)
                if not physical_enabled:
                    n.record_heartbeat(self.id, self.tick_counter)
                    continue
                dx = self.x - n.x
                dy = self.y - n.y
                dz = (self.floor - n.floor) * 100
                d = (dx**2 + dy**2 + dz**2)**0.5
                
                d_min = 50.0
                d_max = getattr(n, 'max_distance', 300.0)
                if d <= d_min:
                    per_dist = 0.0
                elif d >= d_max:
                    per_dist = 1.0
                else:
                    per_dist = (d - d_min) / (d_max - d_min) if d_max > d_min else 0.0
                    
                sender_near_fire = any(nb.prev_on_fire for nb in self.neighbors)
                receiver_near_fire = any(nb.prev_on_fire for nb in n.neighbors)
                fire_penalty_val = getattr(n, 'fire_penalty', 0.4)
                fire_penalty = fire_penalty_val if (sender_near_fire or receiver_near_fire) else 0.0
                
                per = min(1.0, per_dist + fire_penalty + n.packet_loss_rate)
                if random.random() < per:
                    continue  # Keepalive heartbeat lost!
                n.record_heartbeat(self.id, self.tick_counter)

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
