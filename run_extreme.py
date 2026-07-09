import time
import sys
from escapemesh.core.network import MeshNetwork
from escapemesh.utils.logger import Tee

def simulate_extreme_building(routing_mode: str):
    print(f"\n==================================================")
    print(f" EXTREME BUILDING SIMULATION: {routing_mode.upper()} ")
    print(f"==================================================")
    
    network = MeshNetwork()
    network.load_from_topology("topologies/topology_extreme.json", routing_mode=routing_mode)
    
    # 1. Initial Convergence
    tick_count = 1
    max_ticks = 80
    print("[PHASE 1] Extreme Building Mesh Convergence Started...")
    
    state_history = []
    while tick_count <= max_ticks:
        snapshot = {name: (node.cost, node.points_to.id if node.points_to else None) 
                    for name, node in network.nodes.items()}
        state_history.append(snapshot)
        if len(state_history) > 12:
            state_history.pop(0)
            
        if len(state_history) >= 10 and all(state_history[i] == state_history[0] for i in range(1, len(state_history))):
            print(f"  > Convergence stabilized. Terminating phase early.")
            break
            
        if not network.tick():
            break
            
        tick_count += 1
        time.sleep(0.01)
        
    print("\n--- INITIAL STABLE ROUTING TABLE (DIVERSE INTERESTING NODES) ---")
    interest_nodes = [
        "F4_Dead1", "F4_H1", "F4_H16", "F3_Dead1", "F3_H1", "F3_H20", 
        "F2_Dead1", "F2_Stairs_C", "F1_Stairs_E", "F1_Stairs_W"
    ]
    for name in interest_nodes:
        if name in network.nodes:
            node = network.nodes[name]
            nxt = node.points_to.id if node.points_to else "None"
            cost_str = f"{node.cost:.1f}" if isinstance(node.cost, float) else f"{node.cost}"
            print(f"  Node {name:15} | Cost/Rank: {cost_str:5} | Next Hop: {nxt}")
            
    # 2. Dual Fire Incident (Blocks multiple exit pathways)
    print("\n[PHASE 2] Incident: Fire detected on Floor 3 West Stairs (F3_Stairs_W) AND Floor 2 East Stairs (F2_Stairs_E)!")
    network.nodes["F3_Stairs_W"].trigger_fire()
    network.nodes["F2_Stairs_E"].trigger_fire()
    
    # 3. Dynamic Rerouting & Self-Healing
    tick_count = 1
    print("[PHASE 3] Dynamic Mesh Rerouting & Self-Healing Started...")
    
    recovery_history = []
    while tick_count <= max_ticks:
        snapshot = {name: (node.cost, node.points_to.id if node.points_to else None) 
                    for name, node in network.nodes.items()}
        recovery_history.append(snapshot)
        if len(recovery_history) > 12:
            recovery_history.pop(0)
            
        if len(recovery_history) >= 12 and all(recovery_history[i] == recovery_history[0] for i in range(1, len(recovery_history))):
            print(f"  > Healing stabilized. Terminating phase early.")
            break
            
        if not network.tick():
            break
            
        tick_count += 1
        time.sleep(0.01)
        
    print("\n--- POST-FIRE STABLE ROUTING TABLE (DIVERSE INTERESTING NODES) ---")
    interest_nodes.extend(["F3_Stairs_W", "F2_Stairs_E"])
    for name in sorted(interest_nodes):
        if name in network.nodes:
            node = network.nodes[name]
            nxt = node.points_to.id if node.points_to else "None"
            cost_str = f"{node.cost:.1f}" if isinstance(node.cost, float) else f"{node.cost}"
            print(f"  Node {name:15} | Cost/Rank: {cost_str:5} | Next Hop: {nxt}")

if __name__ == "__main__":
    # Setup Tee logging to capture everything to a file while keeping console output
    sys.stdout = Tee("logs/simulation_extreme.log")
    
    # Run the simulation on all 6 routing algorithms
    simulate_extreme_building("gradient")
    simulate_extreme_building("link_state")
    simulate_extreme_building("dsdv")
    simulate_extreme_building("aodv")
    simulate_extreme_building("potential_field")
    simulate_extreme_building("rpl")
