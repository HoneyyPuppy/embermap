from typing import List, Dict, Tuple, Optional
from escapemesh.core.base_node import Node, INF

class RPLNode(Node):
    """
    Node implementing RPL (Routing Protocol for Low-Power and Lossy Networks).
    Constructs a Destination-Oriented Directed Acyclic Graph (DODAG) rooted at the exit node.
    """
    def __init__(self, node_id: str, is_exit: bool = False):
        super().__init__(node_id, is_exit)
        # RPL Rank (exits have a fixed Rank of 1.0, representing the DODAG root)
        self.rank: float = 1.0 if is_exit else float(INF)
        self.cost = self.rank  # Map cost variable to Rank for unified print formatting
        
        self.preferred_parent: Optional[Node] = None
        self.parent_set: Dict[str, float] = {}  # parent_node_id -> parent_rank
        self.rpl_tick_count = 0
        self.sequence_num = 0

    def on_fire_action(self):
        # Poison our rank to INF and broadcast DIO to force child nodes to switch parents
        self.rank = float(INF)
        self.cost = float(INF)
        self.preferred_parent = None
        self.broadcast_dio()

    def broadcast_dio(self):
        """Broadcasts a DODAG Information Object (DIO) to neighbors."""
        for n in self.neighbors:
            if not n.on_fire:
                n.incoming_dios.append_from(self, (self.id, self.rank))

    def tick(self) -> bool:
        if self.on_fire:
            return False

        self.rpl_tick_count += 1

        if self.is_exit:
            # Root periodically sends DIOs downwards to refresh the DAG
            if self.sequence_num == 0 or self.rpl_tick_count % 3 == 0:
                self.sequence_num += 1
                self.broadcast_dio()
            return False

        # 1. Process received DIOs
        dios = self.incoming_dios[:]
        self.incoming_dios.clear()
        dios_processed = False

        for sender_id, sender_rank in dios:
            if sender_rank == INF:
                # Parent poisoned its rank, remove it
                if sender_id in self.parent_set:
                    self.parent_set.pop(sender_id)
                    dios_processed = True
            else:
                # Add or update parent rank in our set
                self.parent_set[sender_id] = sender_rank
                dios_processed = True

        # 2. Local link break detection via Heartbeat timeout
        active_neighbors = self.get_active_neighbors()
        active_set = set(active_neighbors)
        
        # Remove any parents that are no longer active/alive
        inactive_parents = [p for p in self.parent_set if p not in active_set]
        for p in inactive_parents:
            self.parent_set.pop(p)
            dios_processed = True

        # 3. Preferred Parent selection & Rank calculation (Objective Function 0 - OF0)
        if dios_processed or (not self.parent_set and self.preferred_parent):
            best_parent_id = None
            best_parent_rank = float(INF)

            for parent_id, p_rank in self.parent_set.items():
                if p_rank < best_parent_rank:
                    best_parent_rank = p_rank
                    best_parent_id = parent_id

            new_parent = None
            if best_parent_id:
                # Find matching neighbor Node object
                for n in self.neighbors:
                    if n.id == best_parent_id:
                        new_parent = n
                        break

            # If we selected a preferred parent, our Rank is parent's Rank + 1.0 (RankIncrease)
            new_rank = best_parent_rank + 1.0 if new_parent else float(INF)
            if new_rank > INF:
                new_rank = float(INF)

            # Update state if rank or preferred parent changed
            if new_rank != self.rank or self.preferred_parent != new_parent:
                self.rank = new_rank
                self.preferred_parent = new_parent
                
                # Trigger console logger state outputs (this will update self.cost and self.points_to)
                self._update_routing_state(new_rank, new_parent)
                
                # Advertise our rank change to downstream nodes immediately
                self.broadcast_dio()
                
                return True

        # Periodic trickle DIO pings (simulating RPL trickle timers)
        if self.rpl_tick_count % 5 == 0:
            self.broadcast_dio()

        return False
