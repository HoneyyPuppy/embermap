import os
import sys

# Add project root to path
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from escapemesh.core.network import MeshNetwork

def test_ls():
    topo_path = os.path.join("topologies", "topology_extreme.json")
    
    # Instantiate network with Link State
    network = MeshNetwork(
        packet_loss_rate=0.0,
        max_distance=99999.0, # Infinity Max Range
        fire_penalty=0.4,
        delay_factor=0.0,
        jitter_ticks=0,
        csma_enabled=False # Disable CSMA/CA
    )
    network.load_from_topology(topo_path, routing_mode="link_state")
    
    print(f"Network initialized with {len(network.nodes)} nodes.")
    
    # Run ticks
    converged = False
    max_ticks = 100
    stability_window = 8
    history = []
    
    for tick in range(1, max_ticks + 1):
        # Capture snapshot
        snap = {
            node_id: {
                "cost": node.cost,
                "next_hop": node.points_to.id if node.points_to else None
            }
            for node_id, node in network.nodes.items()
        }
        history.append(snap)
        if len(history) > stability_window + 2:
            history.pop(0)
            
        if len(history) >= stability_window and all(history[i] == history[0] for i in range(1, len(history))):
            print(f"--> CONVERGED at tick {tick}!")
            converged = True
            break
            
        changed = network.tick()
        
        # Count how many nodes changed state
        changed_nodes = 0
        if len(history) >= 2:
            prev = history[-2]
            for nid, state in snap.items():
                if prev.get(nid) != state:
                    changed_nodes += 1
                    
        # Check active LSA count in staged queues
        total_staged_lsas = sum(len(node.incoming_lsas.staged) for node in network.nodes.values())
        total_active_lsas = sum(len(node.incoming_lsas) for node in network.nodes.values())
        
        print(f"Tick {tick:02d}: changed_nodes={changed_nodes}, total_staged_lsas={total_staged_lsas}, total_active_lsas={total_active_lsas}")
        
        # If no changes and no LSAs in queues, print debug
        if changed_nodes == 0 and total_staged_lsas == 0 and total_active_lsas == 0:
            print(f"   [Idle] No nodes changed, no LSAs in queue.")
            
    if not converged:
        print("--> FAILED TO CONVERGE within 100 ticks!")
        # Print a few nodes' routing tables
        for nid in list(network.nodes.keys())[:5]:
            node = network.nodes[nid]
            print(f"Node {nid}: cost={node.cost}, next_hop={node.points_to.id if node.points_to else None}, neighbors={len(node.neighbors)}")

if __name__ == "__main__":
    test_ls()
