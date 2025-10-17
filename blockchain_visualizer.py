#!/usr/bin/env python3
import csv
import networkx as nx
import matplotlib.pyplot as plt
from collections import defaultdict
import sys

def load_blockchain_data(filename):
    nodes = {}
    edges = []
    genesis_nodes = []
    
    with open(filename, 'r') as f:
        reader = csv.DictReader(f)
        for row in reader:
            seq = row['seq']
            curr_hash = row['curr_hash_hex']
            prev_hash = row['prev_hash_hex']
            status = row['chain_status']
            timestamp = row['insert_timestamp']
            
            # Store node info
            nodes[curr_hash] = {
                'seq': seq,
                'timestamp': timestamp,
                'status': status,
                'data': row['random_data']
            }
            
            # Create edge from prev to curr
            if prev_hash != '0000000000000000000000000000000000000000000000000000000000000000':
                edges.append((prev_hash, curr_hash))
            else:
                genesis_nodes.append(curr_hash)
    
    return nodes, edges, genesis_nodes

def create_graph(nodes, edges, genesis_nodes):
    G = nx.DiGraph()
    
    # Add nodes with attributes
    for hash_val, info in nodes.items():
        G.add_node(hash_val, **info)
    
    # Add edges
    G.add_edges_from(edges)
    
    return G

def find_chains(G, genesis_nodes):
    chains = []
    for genesis in genesis_nodes:
        if genesis in G:
            # Find all descendants
            descendants = nx.descendants(G, genesis)
            chain = {genesis}.union(descendants)
            chains.append(chain)
    return chains

def visualize_blockchain(G, nodes, genesis_nodes):
    # Create figure
    plt.figure(figsize=(20, 12))
    
    # Use hierarchical layout
    pos = nx.spring_layout(G, k=3, iterations=50)
    
    # Color nodes based on their properties
    node_colors = []
    node_sizes = []
    
    for node in G.nodes():
        if node in genesis_nodes:
            node_colors.append('red')
            node_sizes.append(800)
        elif G.in_degree(node) > 1:  # Node with multiple parents
            node_colors.append('orange')
            node_sizes.append(600)
        elif G.out_degree(node) > 1:  # Node with multiple children
            node_colors.append('yellow')
            node_sizes.append(600)
        else:
            node_colors.append('lightblue')
            node_sizes.append(400)
    
    # Draw the graph
    nx.draw_networkx_edges(G, pos, edge_color='gray', arrows=True, 
                          arrowsize=20, arrowstyle='->', width=1.5)
    nx.draw_networkx_nodes(G, pos, node_color=node_colors, 
                          node_size=node_sizes, alpha=0.8)
    
    # Add labels for important nodes
    labels = {}
    for node in G.nodes():
        if node in genesis_nodes:
            labels[node] = f"Genesis\n{nodes[node]['seq']}"
        elif G.in_degree(node) > 1 or G.out_degree(node) > 1:
            labels[node] = f"Seq: {nodes[node]['seq']}"
    
    nx.draw_networkx_labels(G, pos, labels, font_size=8)
    
    plt.title("Blockchain Hash Chain Visualization", fontsize=16)
    plt.axis('off')
    
    # Add legend
    from matplotlib.patches import Patch
    legend_elements = [
        Patch(facecolor='red', label='Genesis Blocks'),
        Patch(facecolor='orange', label='Merge Points (Multiple Parents)'),
        Patch(facecolor='yellow', label='Fork Points (Multiple Children)'),
        Patch(facecolor='lightblue', label='Regular Blocks')
    ]
    plt.legend(handles=legend_elements, loc='upper left')
    
    plt.tight_layout()
    plt.show()

def analyze_blockchain(G, nodes, genesis_nodes):
    print("\n=== Blockchain Analysis ===\n")
    
    print(f"Total blocks: {len(nodes)}")
    print(f"Genesis blocks: {len(genesis_nodes)}")
    
    # Find chains
    chains = find_chains(G, genesis_nodes)
    print(f"\nNumber of separate chains: {len(chains)}")
    for i, chain in enumerate(chains):
        print(f"  Chain {i+1}: {len(chain)} blocks")
    
    # Find nodes with multiple parents (merge points)
    merge_points = [node for node in G.nodes() if G.in_degree(node) > 1]
    print(f"\nMerge points (blocks with multiple parents): {len(merge_points)}")
    for node in merge_points[:5]:  # Show first 5
        parents = list(G.predecessors(node))
        print(f"  Block {nodes[node]['seq']}: {len(parents)} parents")
    
    # Find nodes with multiple children (fork points)
    fork_points = [node for node in G.nodes() if G.out_degree(node) > 1]
    print(f"\nFork points (blocks with multiple children): {len(fork_points)}")
    for node in fork_points[:5]:  # Show first 5
        children = list(G.successors(node))
        print(f"  Block {nodes[node]['seq']}: {len(children)} children")
    
    # Find longest chain
    longest_path = []
    for genesis in genesis_nodes:
        if genesis in G:
            for node in G.nodes():
                if G.out_degree(node) == 0:  # Leaf node
                    try:
                        path = nx.shortest_path(G, genesis, node)
                        if len(path) > len(longest_path):
                            longest_path = path
                    except nx.NetworkXNoPath:
                        pass
    
    print(f"\nLongest chain: {len(longest_path)} blocks")
    if longest_path:
        print(f"  From block {nodes[longest_path[0]]['seq']} to block {nodes[longest_path[-1]]['seq']}")

def create_hierarchical_layout(G, genesis_nodes):
    """Create a hierarchical layout with genesis nodes at the top"""
    pos = {}
    y_offset = 0
    x_spacing = 4
    
    # Process each chain separately
    for i, genesis in enumerate(genesis_nodes):
        if genesis not in G:
            continue
            
        # Get all nodes in this chain using BFS
        visited = set()
        queue = [(genesis, 0)]  # (node, level)
        levels = defaultdict(list)
        
        while queue:
            node, level = queue.pop(0)
            if node in visited:
                continue
            visited.add(node)
            levels[level].append(node)
            
            # Add children to queue
            for child in G.successors(node):
                queue.append((child, level + 1))
        
        # Position nodes by level
        x_offset = i * x_spacing * 3  # Space between chains
        for level, nodes_at_level in levels.items():
            y = -level * 0.5  # Negative to make tree grow downward
            for j, node in enumerate(nodes_at_level):
                x = x_offset + (j - len(nodes_at_level)/2) * 0.5
                pos[node] = (x, y)
    
    return pos

def create_simplified_view(G, nodes, max_nodes=100):
    """Create a simplified view focusing on the most interesting parts"""
    plt.figure(figsize=(20, 14))
    
    # Get genesis nodes
    genesis_nodes = [n for n in G.nodes() if G.in_degree(n) == 0]
    
    # Create hierarchical layout
    pos = create_hierarchical_layout(G, genesis_nodes)
    
    # If we have too many nodes, create a subgraph
    if len(G.nodes()) > max_nodes:
        # Select interesting nodes
        interesting_nodes = set()
        
        # Add genesis nodes
        interesting_nodes.update(genesis_nodes)
        
        # Add fork points
        fork_points = [n for n in G.nodes() if G.out_degree(n) > 1]
        interesting_nodes.update(fork_points[:20])
        
        # For each genesis, add some descendants
        for genesis in genesis_nodes:
            if genesis in G:
                # Add first few levels of descendants
                queue = [(genesis, 0)]
                visited = set()
                while queue and len(interesting_nodes) < max_nodes:
                    node, level = queue.pop(0)
                    if node in visited or level > 5:  # Limit depth
                        continue
                    visited.add(node)
                    interesting_nodes.add(node)
                    for child in G.successors(node):
                        queue.append((child, level + 1))
        
        # Create subgraph
        subG = G.subgraph(interesting_nodes)
        
        # Update positions to only include subgraph nodes
        pos = {node: pos[node] for node in subG.nodes() if node in pos}
    else:
        subG = G
    
    # Draw edges
    nx.draw_networkx_edges(subG, pos, edge_color='gray', arrows=True, 
                          arrowsize=10, width=1, alpha=0.6)
    
    # Draw nodes with colors
    node_colors = []
    node_sizes = []
    for node in subG.nodes():
        if node in genesis_nodes:
            node_colors.append('red')
            node_sizes.append(500)
        elif G.out_degree(node) > 1:
            node_colors.append('yellow')
            node_sizes.append(400)
        else:
            node_colors.append('lightblue')
            node_sizes.append(300)
    
    nx.draw_networkx_nodes(subG, pos, node_color=node_colors, 
                          node_size=node_sizes, alpha=0.8)
    
    # Add labels
    labels = {}
    for node in subG.nodes():
        if node in genesis_nodes:
            labels[node] = f"Genesis {nodes[node]['seq']}"
        elif G.out_degree(node) > 1:
            labels[node] = f"Fork {nodes[node]['seq']}"
        else:
            # Only label some regular nodes to avoid clutter
            if subG.in_degree(node) == 0 or subG.out_degree(node) == 0:
                labels[node] = nodes[node]['seq']
    
    nx.draw_networkx_labels(subG, pos, labels, font_size=8)
    
    # Add title and legend
    plt.title("Blockchain Tree Structure (Genesis blocks at top)", fontsize=16)
    
    from matplotlib.patches import Patch
    legend_elements = [
        Patch(facecolor='red', label='Genesis Blocks'),
        Patch(facecolor='yellow', label='Fork Points'),
        Patch(facecolor='lightblue', label='Regular Blocks')
    ]
    plt.legend(handles=legend_elements, loc='upper right')
    
    plt.axis('off')
    plt.tight_layout()
    plt.show()

def create_tree_view(G, nodes, genesis_nodes):
    """Create a clean tree view for each chain"""
    num_chains = len(genesis_nodes)
    fig, axes = plt.subplots(1, min(num_chains, 3), figsize=(18, 10))
    
    if num_chains == 1:
        axes = [axes]
    elif num_chains == 2:
        axes = axes[:2]
    
    for i, genesis in enumerate(genesis_nodes[:3]):
        if genesis not in G:
            continue
            
        # Get all descendants of this genesis
        descendants = nx.descendants(G, genesis)
        chain_nodes = {genesis}.union(descendants)
        subG = G.subgraph(chain_nodes)
        
        # Create hierarchical layout for this chain
        try:
            if len(chain_nodes) < 100:
                pos = nx.nx_agraph.graphviz_layout(subG, prog='dot')
            else:
                pos = create_hierarchical_layout(subG, [genesis])
        except:
            # Fallback if graphviz not available
            pos = create_hierarchical_layout(subG, [genesis])
        
        # Draw on subplot
        ax = axes[i] if num_chains > 1 else axes
        plt.sca(ax)
        
        # Draw edges and nodes
        nx.draw_networkx_edges(subG, pos, edge_color='gray', arrows=True, ax=ax)
        
        node_colors = []
        for node in subG.nodes():
            if node == genesis:
                node_colors.append('red')
            elif subG.out_degree(node) > 1:
                node_colors.append('yellow')
            else:
                node_colors.append('lightblue')
        
        nx.draw_networkx_nodes(subG, pos, node_color=node_colors, node_size=200, ax=ax)
        
        # Add minimal labels
        labels = {}
        labels[genesis] = f"G{nodes[genesis]['seq']}"
        for node in subG.nodes():
            if subG.out_degree(node) > 1:
                labels[node] = nodes[node]['seq']
        
        nx.draw_networkx_labels(subG, pos, labels, font_size=7, ax=ax)
        
        ax.set_title(f"Chain {i+1} ({len(chain_nodes)} blocks)", fontsize=12)
        ax.axis('off')
    
    plt.suptitle("Individual Blockchain Chains", fontsize=16)
    plt.tight_layout()
    plt.show()

def main():
    filename = 'hash_chain_export.csv'
    
    print("Loading blockchain data...")
    nodes, edges, genesis_nodes = load_blockchain_data(filename)
    
    print("Creating graph...")
    G = create_graph(nodes, edges, genesis_nodes)
    
    # Analyze the blockchain
    analyze_blockchain(G, nodes, genesis_nodes)
    
    # Create visualizations
    print("\nGenerating visualizations...")
    
    # Show individual chain views
    create_tree_view(G, nodes, genesis_nodes)
    
    # Show unified hierarchical view
    create_simplified_view(G, nodes)
    
    print("\nVisualization complete!")

if __name__ == "__main__":
    main()