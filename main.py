import time
from escapemesh.core.network import MeshNetwork

def simulate_algorithm(routing_mode: str):
    print(f"\n==================================================")
    print(f" RUNNING SIMULATION: {routing_mode.upper()} ROUTING ")
    print(f"==================================================")
    
    network = MeshNetwork()
    network.load_from_topology("topology.json", routing_mode=routing_mode)
    
    # 1. Converge network
    tick_count = 1
    max_ticks = 50
    print("[PHASE 1] Network Convergence Started...")
    while network.tick() and tick_count <= max_ticks:
        print(f"  > Tick {tick_count} complete.")
        tick_count += 1
        time.sleep(0.05)
        
    print("\n--- INITIAL STABLE ROUTING TABLE ---")
    for name in sorted(network.nodes.keys()):
        node = network.nodes[name]
        nxt = node.points_to.id if node.points_to else "None"
        cost_str = f"{node.cost:.1f}" if isinstance(node.cost, float) else f"{node.cost}"
        print(f"  Node {name:6} | Cost: {cost_str:5} | Next Hop: {nxt}")
        
    # 2. Trigger fire on N2 (Breaks path to EXIT for N1)
    print("\n[PHASE 2] Simulating Incident: Fire Detected on N2...")
    network.nodes["N2"].trigger_fire()
    
    # 3. Re-converge network
    tick_count = 1
    print("[PHASE 3] Network Self-Healing / Recovery Started...")
    while network.tick() and tick_count <= max_ticks:
        print(f"  > Recovery Tick {tick_count} complete.")
        tick_count += 1
        time.sleep(0.05)
        
    print("\n--- POST-FIRE STABLE ROUTING TABLE ---")
    for name in sorted(network.nodes.keys()):
        node = network.nodes[name]
        nxt = node.points_to.id if node.points_to else "None"
        cost_str = f"{node.cost:.1f}" if isinstance(node.cost, float) else f"{node.cost}"
        print(f"  Node {name:6} | Cost: {cost_str:5} | Next Hop: {nxt}")


if __name__ == "__main__":
    # Simulate routing algorithms to compare behaviour
    simulate_algorithm("gradient")
    simulate_algorithm("link_state")
    simulate_algorithm("dsdv")
    simulate_algorithm("aodv")
    simulate_algorithm("potential_field")
