import time
from escapemesh.core.network import MeshNetwork

def simulate_complex_building(routing_mode: str):
    print(f"\n==================================================")
    print(f" COMPLEX BUILDING SIMULATION: {routing_mode.upper()} ")
    print(f"==================================================")
    
    network = MeshNetwork()
    network.load_from_topology("topologies/topology_complex.json", routing_mode=routing_mode)
    
    # 1. Initial Convergence
    tick_count = 1
    max_ticks = 60
    print("[PHASE 1] Building Mesh Convergence Started...")
    
    state_history = []
    while tick_count <= max_ticks:
        snapshot = {name: (node.cost, node.points_to.id if node.points_to else None) 
                    for name, node in network.nodes.items()}
        state_history.append(snapshot)
        if len(state_history) > 10:
            state_history.pop(0)
            
        if len(state_history) >= 8 and all(state_history[i] == state_history[0] for i in range(1, len(state_history))):
            print(f"  > Convergence stabilized. Terminating phase early.")
            break
            
        if not network.tick():
            break
            
        tick_count += 1
        time.sleep(0.02)
        
    print("\n--- INITIAL STABLE ROUTING TABLE (FLOOR 3 SELECTED) ---")
    for name in sorted(network.nodes.keys()):
        if name.startswith("F3_"):
            node = network.nodes[name]
            nxt = node.points_to.id if node.points_to else "None"
            cost_str = f"{node.cost:.1f}" if isinstance(node.cost, float) else f"{node.cost}"
            print(f"  Node {name:15} | Cost/Rank: {cost_str:5} | Next Hop: {nxt}")
            
    # 2. Fire breaks out on Floor 2 West Stairs (blocking West descent)
    print("\n[PHASE 2] Incident: Fire detected on Floor 2 West Stairs (F2_Stairs_West)!")
    network.nodes["F2_Stairs_West"].trigger_fire()
    
    # 3. Dynamic Rerouting & Self-Healing
    tick_count = 1
    print("[PHASE 3] Dynamic Mesh Rerouting & Self-Healing Started...")
    
    recovery_history = []
    while tick_count <= max_ticks:
        snapshot = {name: (node.cost, node.points_to.id if node.points_to else None) 
                    for name, node in network.nodes.items()}
        recovery_history.append(snapshot)
        if len(recovery_history) > 10:
            recovery_history.pop(0)
            
        if len(recovery_history) >= 12 and all(recovery_history[i] == recovery_history[0] for i in range(1, len(recovery_history))):
            print(f"  > Healing stabilized. Terminating phase early.")
            break
            
        if not network.tick():
            break
            
        tick_count += 1
        time.sleep(0.02)
        
    print("\n--- POST-FIRE STABLE ROUTING TABLE (FLOOR 3 SELECTED) ---")
    for name in sorted(network.nodes.keys()):
        if name.startswith("F3_") or name == "F2_Stairs_West":
            node = network.nodes[name]
            nxt = node.points_to.id if node.points_to else "None"
            cost_str = f"{node.cost:.1f}" if isinstance(node.cost, float) else f"{node.cost}"
            print(f"  Node {name:15} | Cost/Rank: {cost_str:5} | Next Hop: {nxt}")

if __name__ == "__main__":
    # Test complex floor-to-floor rerouting on selected protocols
    simulate_complex_building("gradient")
    simulate_complex_building("rpl")
    simulate_complex_building("aodv")
