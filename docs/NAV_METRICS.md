# Navigation-mesh quality & optimization metrics

Research-backed reference for the metrics `navtools_analyze_graph` computes, why
they matter, and which need extra inputs. Almost everything useful is computable
**offline from a parsed `.nav`** (plus, for staleness, the `.bsp` size); only
pathfinding-cost metrics need simulation.

A Source `.nav` area is an **axis-aligned rectangle** (nwCorner/seCorner + two
corner Z's), so it is always convex — the classic polygon-shape-quality
literature (slivers/convexity/degeneracy) mostly does not apply, and the
Source-relevant shape signals reduce to **size** and **aspect ratio**.

## Tiers

| input needed | metrics |
|---|---|
| `.nav` alone | header/version/analyzed, counts, attribute distribution, place coverage, connectivity graph (components, SCCs, dead ends, articulation points, bridges, betweenness, diameter), one-way links, size/aspect distribution, drop/jump validation |
| `.nav` + `.bsp` | **staleness** (stored vs actual BSP size), coverage vs walkable floor* |
| simulation (A*) | path suboptimality, node expansions, query latency, smoothness |

\* coverage-vs-floor needs parsing BSP walkable faces (not yet implemented).

## Metric definitions

### Integrity / staleness — `.nav` (+`.bsp`)
- **Magic / version / sub_version**: `0xFEEDFACE`, current version 16, TF2 sub_version 2. [nav_file.cpp]
- **BSP staleness**: the `.nav` stores the source BSP byte size; if it differs from the actual `.bsp`, the engine flags the mesh out-of-date. Decisive correctness check. [nav_file.cpp]
- **Analyzed flag** (v14+): unanalyzed ⇒ hiding/encounter/visibility/light data unreliable.
- **Generation-only JUMP leak**: areas still carrying `NAV_MESH_JUMP (0x2)` in a finished mesh are suspicious — that flag is "only used during generation." [nav.h]

### Counts & attributes — `.nav`
- Areas, connections, ladders, hiding spots, encounter paths, place-dictionary size, **place coverage %** (areas with a non-zero place index).
- **Attribute distribution** (% of areas per flag). Red flags: very high CROUCH (sampled low geometry), stairs geometry without STAIRS areas (bot stutter), persisted JUMP. Bitflags verified against `nav.h`.

### Geometry — `.nav`
- **Area size distribution** (units²: mean/median/std) — high variance signals mixed over/under-segmentation. [van Toll, MiG'16]
- **Aspect ratio** = max(dx,dy)/min(dx,dy) — long/thin areas cause detour paths (Recast notes long-thin polys → "paths with detours"). Source-appropriate substitute for sliver detection.
- **Too-small** (dim < `GenerationStepSize` 25u) = over-segmentation; **too-large** = should be split (attributes apply to the whole area). [VDC; nav_generate.cpp]
- **Drop/jump validation** (approx via area z-centers): up-links above `JumpCrouchHeight` (58u) likely unreachable; down-links below `−DeathDrop` (200u) are fatal/one-way. Constants verified against `nav.h` (the 64/400 values are L4D `#ifdef TERROR` only).

### Connectivity graph — `.nav` (areas = nodes, connections = directed edges)
- **Weak components & islands**: expect 1; any area outside the main component is present-but-unreachable (the canonical navmesh bug). [Wikipedia: Navigation mesh]
- **Strongly-connected components (Tarjan, O(V+E))**: areas not in the main SCC are reachable but may be one-way **traps** (common with drops). [Tarjan SCC]
- **One-way connections**: directed edge (u,v) without (v,u).
- **Dead ends**: out-degree 0 (sink), in-degree 0 (source-only), total degree 1, fully isolated.
- **Articulation points & bridges (Tarjan, O(V+E))**: structural chokepoints — one area/edge whose removal splits the map. [Bridge (graph theory)]
- **Betweenness centrality (Brandes, O(V·E); sampled if large)**: ranks traffic chokepoints/crossroads. [Neo4j GDS]
- **Diameter / average path length** (per main component): stringiness indicator.

### Pathfinding performance — simulation (future `navtools_pathcheck`)
- **Suboptimality** `found/optimal` (≥1) isolates planner quality from map difficulty; rectangular/grid paths run ~8% longer in 2D (~13% 3D) than true shortest. [GameAIPro Theta*; Any-angle planning]
- **A\* node expansions** (machine-independent), peak open-set (memory), query latency.
- **Smoothness**: cumulative turning angle, waypoint count after string-pulling.
- **Methodology** (Moving AI / GPPC): random start/goal sampling, **bucket by optimal length** and report per-bucket, store a reference optimal cost per query, **validate every path** (on-mesh, connected, endpoints match), **≥5 non-consecutive repeats**, report a metric vector / Pareto frontier rather than one score. [Sturtevant 2012; GPPC 2014]

## Health score
`navtools_analyze_graph` emits a transparent 0–100 score: it starts at 100 and
deducts for staleness, unanalyzed state, disconnected islands, one-way-trap
areas, fatal drops / unclimbable links, sub-grid and high-aspect areas, and
persisted generation flags, listing each as a red flag. Thresholds are
CLI-overridable.

## Tuning generation with these metrics
Granularity is the master accuracy↔cost knob (Source `GenerationStepSize` 25u;
Recast `cellSize` ≈ agentRadius/2..3; Unity documents halving voxel size = 4×
cost). Protocol: change one parameter → regenerate → diff the metric vector
(`navtools_analyze_graph --compare` or `--json`): "better" = coverage up,
size-variance / sub-grid / high-aspect down, components → 1, path cost flat or
down. Edge-to-wall gaps ≈ agent radius are expected and must not be flagged.

## Sources
van Toll et al., *A Comparative Study of Navigation Meshes* (MiG'16)
https://webspace.science.uu.nl/~gerae101/pdf/comparative_study_of_navigation_meshes.pdf ·
Recast `rcConfig` https://recastnav.com/structrcConfig.html ·
Recast Settings Uncovered http://digestingduck.blogspot.com/2009/08/recast-settings-uncovered.html ·
Wikipedia: Navigation mesh https://en.wikipedia.org/wiki/Navigation_mesh ·
Bridge (graph theory) https://en.wikipedia.org/wiki/Bridge_(graph_theory) ·
Tarjan SCC https://en.wikipedia.org/wiki/Tarjan%27s_strongly_connected_components_algorithm ·
Brandes betweenness (Neo4j GDS) https://neo4j.com/docs/graph-data-science/current/algorithms/betweenness-centrality/ ·
Any-angle path planning https://en.wikipedia.org/wiki/Any-angle_path_planning ·
GameAIPro Theta* https://www.gameaipro.com/GameAIPro2/GameAIPro2_Chapter16_Theta_Star_for_Any-Angle_Pathfinding.pdf ·
Sturtevant, *Benchmarks for Grid-Based Pathfinding* https://webdocs.cs.ualberta.ca/~nathanst/papers/benchmarks.pdf ·
GPPC 2014 https://webdocs.cs.ualberta.ca/~nathanst/papers/GPPC-2014.pdf ·
Unity AI Navigation https://docs.unity3d.com/540/Documentation/Manual/nav-AdvancedSettings.html ·
Unreal navmesh optimization https://dev.epicgames.com/documentation/unreal-engine/optimizing-navigation-mesh-generation-speed-in-unreal-engine ·
Source `nav.h` / `nav_file.cpp` (verified locally) https://github.com/ValveSoftware/source-sdk-2013/blob/master/mp/src/game/server/nav.h ·
VDC NAV format https://developer.valvesoftware.com/wiki/NAV_(file_format)
