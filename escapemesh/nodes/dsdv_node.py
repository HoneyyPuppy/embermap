from typing import List, Tuple, Dict, Optional, Set
from escapemesh.core.base_node import Node, INF

class DSDVNode(Node):
    """Node implementing Destination-Sequenced Distance-Vector routing."""
    def __init__(self, node_id: str, is_exit: bool = False):
        super().__init__(node_id, is_exit)
        self.routing_table: Dict[str, Tuple[float, int, Optional[str]]] = {}
        self.sequence_num = 0
        self.dsdv_tick_count = 0
        self.last_active_neighbors: List[str] = []
        if is_exit:
            self.routing_table[self.id] = (0.0, self.sequence_num, None)

    def on_fire_action(self):
        for dest in list(self.routing_table.keys()):
            m, s, nh = self.routing_table[dest]
            self.routing_table[dest] = (float(INF), s + 1, None)
        self.broadcast_dsdv_update()

    def broadcast_dsdv_update(self):
        update_packet = {dest: (m, s) for dest, (m, s, _) in self.routing_table.items()}
        for n in self.neighbors:
            n.incoming_dsdv_updates.append((self.id, update_packet))

    def tick(self) -> bool:
        if self.on_fire:
            return False

        self.dsdv_tick_count += 1

        if self.is_exit:
            if self.sequence_num == 0 or self.dsdv_tick_count % 5 == 0:
                self.sequence_num += 2
                self.routing_table[self.id] = (0.0, self.sequence_num, None)
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
                        self.routing_table[dest] = (float(INF), s + 1, None)
                        link_broken = True
        self.last_active_neighbors = active_now

        updates_processed = False
        current_updates = self.incoming_dsdv_updates[:]
        self.incoming_dsdv_updates.clear()

        for neighbor_id, table in current_updates:
            for dest, (metric_rec, seq_rec) in table.items():
                new_metric = metric_rec + 1.0 if metric_rec != INF else float(INF)
                current_metric, current_seq, current_nh = self.routing_table.get(
                    dest, (float(INF), -1, None)
                )

                is_broken_route = (seq_rec % 2 != 0)
                should_up = False
                
                if not is_broken_route:
                    if seq_rec > current_seq:
                        should_up = True
                    elif seq_rec == current_seq:
                        should_up = new_metric < current_metric
                else:
                    if current_nh == neighbor_id:
                        should_up = True
                        new_metric = float(INF)

                if should_up:
                    self.routing_table[dest] = (new_metric, seq_rec, neighbor_id if new_metric != INF else None)
                    updates_processed = True

        if updates_processed or link_broken or (not self.routing_table and self.neighbors):
            self.broadcast_dsdv_update()
            
            best_cost = float(INF)
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
