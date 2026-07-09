import time
from escapemesh.core.network import MeshNetwork

def run_simulation():
    print("--- STARTING MODULAR ESCAPEMESH SIMULATION ---")
    network = MeshNetwork()
    network.load_from_topology("topology.json")
    
    # Converge network
    tick_count = 1
    while network.tick():
        print(f"Network convergence tick {tick_count}...")
        tick_count += 1
        time.sleep(0.05)
        
    print("\n--- STABLE STATE ---")
    for name, node in network.nodes.items():
        nxt = node.points_to.id if node.points_to else "None"
        print(f"Node {name} -> Cost: {node.cost}, Next Hop: {nxt}")
        
    # Trigger fire
    print("\n--- TRIGGERING FIRE ON N2 ---")
    network.nodes["N2"].trigger_fire()
    
    # Re-converge network
    tick_count = 1
    while network.tick():
        print(f"Network recovery tick {tick_count}...")
        tick_count += 1
        time.sleep(0.05)
        
    print("\n--- POST-FIRE STABLE STATE ---")
    for name, node in network.nodes.items():
        nxt = node.points_to.id if node.points_to else "None"
        print(f"Node {name} -> Cost: {node.cost}, Next Hop: {nxt}")

if __name__ == "__main__":
    run_simulation()
