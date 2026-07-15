import random
from typing import List, Optional, Dict
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
        
        # 1. Group expired packets by their scheduled delivery_tick
        expired_by_tick = {}
        undelivered = []
        for delivery_tick, sender_id, item in self.staged:
            if delivery_tick <= current_tick:
                expired_by_tick.setdefault(delivery_tick, []).append((sender_id, item))
            else:
                undelivered.append((delivery_tick, sender_id, item))
        
        # 2. Check for collisions at each tick
        for tick_val, packets in expired_by_tick.items():
            # If multiple packets from DIFFERENT senders arrive at the exact same tick, they collide!
            unique_senders = set(sender_id for sender_id, _ in packets if sender_id is not None)
            if len(unique_senders) > 1:
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

class Node:
    """Base IoT node class with built-in Heartbeat keepalive simulation."""
    def __init__(self, node_id: str, is_exit: bool = False, packet_loss_rate: float = 0.0):
        self.id = node_id
        self.is_exit = is_exit
        self.cost: float = 0.0 if is_exit else float(INF)
        self.points_to: Optional['Node'] = None
        self.neighbors: List['Node'] = []
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
        
        # Jacobi frozen states
        self.prev_cost: float = self.cost
        self.prev_points_to: Optional['Node'] = self.points_to
        self.prev_on_fire: bool = self.on_fire
        
        # Staged packet queues for synchronous update with packet loss check
        self.incoming_lsas = JacobiQueue(self)
        self.incoming_dsdv_updates = JacobiQueue(self)
        self.incoming_aodv_packets = JacobiQueue(self)
        self.incoming_dios = JacobiQueue(self)
        
        # Heartbeat tracking state
        self.tick_counter = 0
        self.last_heartbeat_from: Dict[str, int] = {}

    def add_neighbor(self, neighbor: 'Node'):
        if neighbor not in self.neighbors:
            self.neighbors.append(neighbor)

    def trigger_fire(self):
        """Simulate local fire detection. Stops sending heartbeats."""
        self.on_fire = True
        self.cost = float(INF)
        self.points_to = None
        ColorLogger.error(f"Node {self.id} detected FIRE!")
        self.on_fire_action()

    def on_fire_action(self):
        """Optional lifecycle hook for subclass-specific fire actions."""
        pass

    def record_heartbeat(self, sender_id: str, tick: int):
        """Records the arrival of a keepalive heartbeat from a neighbor."""
        self.last_heartbeat_from[sender_id] = tick

    def pre_tick(self):
        """Lifecycle hook run before execution. Increments ticks and broadcasts keepalives."""
        self.tick_counter += 1
        self.is_transmitting = False
        
        # Freeze cost state for Jacobi synchronous update
        self.prev_cost = self.cost
        self.prev_points_to = self.points_to
        self.prev_on_fire = self.on_fire
        
        # Release staged packets for Jacobi synchronous message passing (based on delivery schedule)
        self.incoming_lsas.swap_staged_to_active(self.tick_counter)
        self.incoming_dsdv_updates.swap_staged_to_active(self.tick_counter)
        self.incoming_aodv_packets.swap_staged_to_active(self.tick_counter)
        self.incoming_dios.swap_staged_to_active(self.tick_counter)
        
        # Broadcast keepalive ping to neighbors every 3 ticks with packet loss check
        if not self.on_fire:
            for n in self.neighbors:
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
        """Runs one CPU cycle step. Returns True if routing state changed."""
        raise NotImplementedError

    def get_active_neighbors(self) -> List[str]:
        """Returns neighbor IDs filtering out dead/fire nodes or those who timed out on heartbeats."""
        if self.on_fire:
            return []
        active = []
        for n in self.neighbors:
            if n.on_fire:
                continue
            # Heartbeat timeout: if a neighbor hasn't sent a keepalive in over 9 ticks (3 missed heartbeats)
            if n.id in self.last_heartbeat_from:
                if self.tick_counter - self.last_heartbeat_from[n.id] > 9:
                    continue
            active.append(n.id)
        return active

    def _update_routing_state(self, new_cost: float, best_neighbor: Optional['Node']) -> bool:
        if new_cost != self.cost or self.points_to != best_neighbor:
            if self.cost != INF and new_cost > self.cost:
                ColorLogger.warn(f"Node {self.id} rerouting. Cost: {self.cost} -> {new_cost}")
            elif new_cost < self.cost:
                ColorLogger.info(f"Node {self.id} found route. Cost: {self.cost} -> {new_cost}")
            self.cost = new_cost
            self.points_to = best_neighbor
            return True
        return False
