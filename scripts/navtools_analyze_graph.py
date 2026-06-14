#!/usr/bin/env python3
"""
navtools_analyze_graph.py - offline quality / connectivity analyzer for Source
Engine navigation meshes (.nav).

Parses the FULL .nav (not just the header) following CNavMesh::Save /
CNavArea::Save in source-sdk-2013 (game/server/nav_file.cpp, nav_area.cpp,
nav_ladder.cpp) and the TF2 extension (game/server/tf/nav_mesh/tf_nav_area.cpp,
which appends one uint32 of TF attributes per area for sub_version >= 1).

It then computes the metrics identified as useful for nav-mesh optimization and
fine-tuning (see docs/NAV_METRICS.md) -- all OFFLINE, no engine required:

  * header / integrity / staleness (optionally vs the source .bsp size)
  * area / connection / ladder / hiding-spot counts, place coverage
  * nav attribute distribution (CROUCH/JUMP/STAIRS/...)
  * area size & aspect-ratio distribution (over/under-segmentation, slivers)
  * drop / jump edge validation against the verified Source constants
  * graph suite: connected components & islands, spawn-less reachability,
    strongly-connected components & one-way "traps", one-way edge count,
    dead ends, articulation points & bridges (chokepoints), betweenness
    centrality (top-N), diameter / average path length
  * a transparent 0-100 health score + red-flag list

Usage:
  navtools_analyze_graph.py <file.nav> [--json] [--bsp <file.bsp>] [--top N]
                            [--include-ladders] [--min-dim U] [--max-dim U]
                            [--max-aspect R]
  navtools_analyze_graph.py --compare <a.nav> <b.nav> [--bsp-a f] [--bsp-b f]

All thresholds are CLI-overridable; defaults come from the SDK constants.
"""
import argparse
import math
import os
import struct
import sys
from collections import deque

NAV_MAGIC = 0xFEEDFACE

# Verified against source-sdk-2013 game/server/nav.h (SDK 2013 / TF defaults;
# the larger #ifdef TERROR values 64/400 are L4D-only and intentionally not used).
GENERATION_STEP = 25.0      # GenerationStepSize  -> min sensible area dimension
STEP_HEIGHT     = 18.0      # StepHeight          -> walk-up limit
JUMP_HEIGHT     = 41.8      # JumpHeight
JUMP_CROUCH     = 58.0      # JumpCrouchHeight    -> max climbable up-link
DEATH_DROP      = 200.0     # DeathDrop           -> fatal fall
CLIFF_HEIGHT    = 300.0     # CliffHeight

# NavAttributeType bit flags (nav.h), verified.
NAV_ATTRS = [
    ("CROUCH",       0x00000001),
    ("JUMP",         0x00000002),  # generation-only; should not persist
    ("PRECISE",      0x00000004),
    ("NO_JUMP",      0x00000008),
    ("STOP",         0x00000010),
    ("RUN",          0x00000020),
    ("WALK",         0x00000040),
    ("AVOID",        0x00000080),
    ("TRANSIENT",    0x00000100),
    ("DONT_HIDE",    0x00000200),
    ("STAND",        0x00000400),
    ("NO_HOSTAGES",  0x00000800),
    ("STAIRS",       0x00001000),
    ("NO_MERGE",     0x00002000),
    ("OBSTACLE_TOP", 0x00004000),
    ("CLIFF",        0x00008000),
]


class NavParseError(Exception):
    pass


# --------------------------------------------------------------------------- #
# Binary reader
# --------------------------------------------------------------------------- #
class Reader:
    def __init__(self, data):
        self.d = data
        self.o = 0

    def need(self, n):
        if self.o + n > len(self.d):
            raise NavParseError("unexpected end of file at offset %d" % self.o)

    def u8(self):  self.need(1); v = self.d[self.o]; self.o += 1; return v
    def u16(self): self.need(2); v = struct.unpack_from("<H", self.d, self.o)[0]; self.o += 2; return v
    def u32(self): self.need(4); v = struct.unpack_from("<I", self.d, self.o)[0]; self.o += 4; return v
    def i32(self): self.need(4); v = struct.unpack_from("<i", self.d, self.o)[0]; self.o += 4; return v
    def f32(self): self.need(4); v = struct.unpack_from("<f", self.d, self.o)[0]; self.o += 4; return v
    def skip(self, n): self.need(n); self.o += n


# --------------------------------------------------------------------------- #
# Parsing
# --------------------------------------------------------------------------- #
def _parse_place_directory(r, version):
    count = r.u16()
    names = []
    for _ in range(count):
        ln = r.u16()
        raw = r.d[r.o:r.o + ln]; r.skip(ln)
        names.append(raw.split(b"\x00", 1)[0].decode("latin-1"))
    has_unnamed = r.u8() if version >= 12 else 0
    return names, has_unnamed


def _parse_area(r, version, tf_extra):
    a = {}
    a["id"] = r.u32()
    a["attr"] = r.i32() if version >= 8 else r.u16()
    nw = (r.f32(), r.f32(), r.f32())
    se = (r.f32(), r.f32(), r.f32())
    a["nw"], a["se"] = nw, se
    a["neZ"] = r.f32()
    a["swZ"] = r.f32()
    # connections N,E,S,W
    conns = []
    for _d in range(4):
        c = r.u32()
        conns.extend(r.u32() for _ in range(c))
    a["conns"] = conns
    # hiding spots: count(u8) then 17 bytes each (id u32, x/y/z f32, flags u8)
    hs = r.u8()
    a["hiding_spots"] = hs
    r.skip(hs * 17)
    # encounter paths
    enc = r.u32()
    a["encounters"] = enc
    for _ in range(enc):
        r.skip(4 + 1 + 4 + 1)             # from, fromDir, to, toDir
        spots = r.u8()
        r.skip(spots * 5)                 # each: id u32 + t u8
    # place index (u16, version >= 5)
    a["place"] = r.u16() if version >= 5 else 0
    # ladder connections: up, down
    lad = 0
    for _ in range(2):
        c = r.u32(); lad += c
        r.skip(c * 4)
    a["ladders"] = lad
    # earliest occupy times (version >= 8): MAX_NAV_TEAMS=2 floats
    if version >= 8:
        a["occupy"] = (r.f32(), r.f32())
    # light intensity (version >= 11): NUM_CORNERS=4 floats
    if version >= 11:
        a["light"] = tuple(r.f32() for _ in range(4))
    # visibility (version >= 16)
    if version >= 16:
        vc = r.u32()
        a["vis_count"] = vc
        r.skip(vc * 5)                    # id u32 + attributes u8
        a["inherit_vis"] = r.u32()
    # TF / derived per-area custom data
    if tf_extra:
        r.skip(tf_extra)
    return a


def _ladder_record_size(version):
    # id,width,top(3),bottom(3),length,dir + 5 area ids  (v16)
    return 4 + 4 + 12 + 12 + 4 + 4 + 5 * 4  # = 60


def parse_nav(path):
    """Full parse with auto-detection of the per-area trailing (TF) bytes."""
    data = open(path, "rb").read()
    r = Reader(data)
    magic = r.u32()
    if magic != NAV_MAGIC:
        raise NavParseError("bad magic 0x%08X (expected 0x%08X)" % (magic, NAV_MAGIC))
    info = {"file": path, "filesize": len(data), "magic": "0x%08X" % magic}
    info["version"] = r.u32()
    info["sub_version"] = r.u32()
    info["bsp_size"] = r.u32()
    info["is_analyzed"] = bool(r.u8()) if info["version"] >= 14 else None
    names, has_unnamed = _parse_place_directory(r, info["version"])
    info["place_names"] = names
    info["has_unnamed_areas"] = bool(has_unnamed)
    area_count = r.u32()
    info["area_count"] = area_count

    # Candidate trailing-byte sizes: subVersion 0 -> 0; TF (1,2) -> 4. Validate
    # by requiring areas + ladder section to land exactly at EOF (SaveCustomData
    # is empty for base + TF meshes).
    guess = 4 if info["sub_version"] >= 1 else 0
    candidates = [guess] + [c for c in (0, 4) if c != guess]
    start = r.o
    areas = None
    chosen = None
    for extra in candidates:
        try:
            rr = Reader(data); rr.o = start
            tmp = [_parse_area(rr, info["version"], extra) for _ in range(area_count)]
            lad_count = rr.u32()
            end = rr.o + lad_count * _ladder_record_size(info["version"])
            # exact EOF (base/TF: empty trailing custom data) is the strong signal
            if 0 <= lad_count < 1_000_000 and end == len(data):
                areas, chosen, info["ladder_count"] = tmp, extra, lad_count
                break
            if areas is None:                      # keep first non-crashing parse as fallback
                areas, chosen, info["ladder_count"] = tmp, extra, lad_count
        except NavParseError:
            continue
    if areas is None:
        raise NavParseError("could not parse area records (unsupported sub_version layout?)")
    info["per_area_trailing_bytes"] = chosen
    info["areas"] = areas
    return info


# --------------------------------------------------------------------------- #
# Graph construction & metrics
# --------------------------------------------------------------------------- #
def build_graph(info, include_ladders=False):
    ids = [a["id"] for a in info["areas"]]
    idx = {aid: i for i, aid in enumerate(ids)}
    n = len(ids)
    out = [[] for _ in range(n)]     # directed adjacency
    und = [set() for _ in range(n)]  # undirected adjacency
    edge_pairs = set()
    for a in info["areas"]:
        u = idx[a["id"]]
        for tid in a["conns"]:
            if tid in idx:
                v = idx[tid]
                out[u].append(v)
                und[u].add(v); und[v].add(u)
                edge_pairs.add((u, v))
    return {"ids": ids, "idx": idx, "n": n, "out": out,
            "und": [sorted(s) for s in und], "edges": edge_pairs}


def connected_components(und, n):
    comp = [-1] * n
    sizes = []
    for s in range(n):
        if comp[s] != -1:
            continue
        c = len(sizes); sizes.append(0)
        dq = deque([s]); comp[s] = c
        while dq:
            u = dq.popleft(); sizes[c] += 1
            for v in und[u]:
                if comp[v] == -1:
                    comp[v] = c; dq.append(v)
    return comp, sizes


def tarjan_scc(out, n):
    """Iterative Tarjan SCC -> list of component id per node, count."""
    index = [0]
    idxs = [-1] * n
    low = [0] * n
    onstk = [False] * n
    stk = []
    comp = [-1] * n
    ncomp = [0]
    for root in range(n):
        if idxs[root] != -1:
            continue
        work = [(root, 0)]
        while work:
            v, pi = work[-1]
            if pi == 0:
                idxs[v] = low[v] = index[0]; index[0] += 1
                stk.append(v); onstk[v] = True
            recursed = False
            i = pi
            while i < len(out[v]):
                w = out[v][i]
                if idxs[w] == -1:
                    work[-1] = (v, i + 1)
                    work.append((w, 0))
                    recursed = True
                    break
                elif onstk[w]:
                    low[v] = min(low[v], idxs[w])
                i += 1
            if recursed:
                continue
            if low[v] == idxs[v]:
                while True:
                    w = stk.pop(); onstk[w] = False; comp[w] = ncomp[0]
                    if w == v:
                        break
                ncomp[0] += 1
            work.pop()
            if work:
                p = work[-1][0]
                low[p] = min(low[p], low[v])
    return comp, ncomp[0]


def articulation_and_bridges(und, n):
    """Iterative DFS for articulation points and bridges (undirected)."""
    disc = [-1] * n
    low = [0] * n
    ap = set()
    bridges = 0
    timer = [0]
    for root in range(n):
        if disc[root] != -1:
            continue
        stack = [(root, -1, 0)]
        child_count = {root: 0}
        while stack:
            u, parent, pi = stack[-1]
            if pi == 0:
                disc[u] = low[u] = timer[0]; timer[0] += 1
            advanced = False
            i = pi
            while i < len(und[u]):
                v = und[u][i]
                if v == parent:
                    i += 1; continue
                if disc[v] == -1:
                    stack[-1] = (u, parent, i + 1)
                    child_count[u] = child_count.get(u, 0) + 1
                    child_count[v] = 0
                    stack.append((v, u, 0))
                    advanced = True
                    break
                else:
                    low[u] = min(low[u], disc[v])
                i += 1
            if advanced:
                continue
            stack.pop()
            if parent != -1:
                low[parent] = min(low[parent], low[u])
                if low[u] > disc[parent]:
                    bridges += 1
                if disc[parent] != disc[root] and low[u] >= disc[parent]:
                    ap.add(parent)
            i = None
        if child_count.get(root, 0) > 1:
            ap.add(root)
    return ap, bridges


def betweenness_top(und, n, comp, sizes, topn, sample_cap=4000):
    """Brandes betweenness on the largest component (sampled if very large)."""
    if n == 0:
        return []
    main = max(range(len(sizes)), key=lambda c: sizes[c])
    nodes = [v for v in range(n) if comp[v] == main]
    bc = {v: 0.0 for v in nodes}
    sources = nodes
    sampled = False
    if len(nodes) > sample_cap:
        step = max(1, len(nodes) // sample_cap)
        sources = nodes[::step]
        sampled = True
    for s in sources:
        S = []
        P = {v: [] for v in nodes}
        sigma = {v: 0.0 for v in nodes}; sigma[s] = 1.0
        dist = {v: -1 for v in nodes}; dist[s] = 0
        Q = deque([s])
        while Q:
            v = Q.popleft(); S.append(v)
            for w in und[v]:
                if dist[w] < 0:
                    dist[w] = dist[v] + 1; Q.append(w)
                if dist[w] == dist[v] + 1:
                    sigma[w] += sigma[v]; P[w].append(v)
        delta = {v: 0.0 for v in nodes}
        while S:
            w = S.pop()
            for v in P[w]:
                delta[v] += (sigma[v] / sigma[w]) * (1.0 + delta[w])
            if w != s:
                bc[w] += delta[w]
    scale = (len(nodes) / len(sources)) if sources else 1.0
    ranked = sorted(((bc[v] * scale, v) for v in nodes), reverse=True)[:topn]
    return [(score, v, sampled) for score, v in ranked]


def diameter_and_avg(und, n, comp, sizes, sample_cap=3000):
    main = max(range(len(sizes)), key=lambda c: sizes[c])
    nodes = [v for v in range(n) if comp[v] == main]
    srcs = nodes
    sampled = False
    if len(nodes) > sample_cap:
        step = max(1, len(nodes) // sample_cap)
        srcs = nodes[::step]; sampled = True
    diameter = 0
    total = 0
    pairs = 0
    for s in srcs:
        dist = {s: 0}
        Q = deque([s])
        while Q:
            u = Q.popleft()
            for v in und[u]:
                if v not in dist:
                    dist[v] = dist[u] + 1; Q.append(v)
        for v, dd in dist.items():
            if dd > 0:
                diameter = max(diameter, dd); total += dd; pairs += 1
    avg = (total / pairs) if pairs else 0.0
    return diameter, avg, sampled


def area_zcenter(a):
    return (a["nw"][2] + a["se"][2] + a["neZ"] + a["swZ"]) / 4.0


# --------------------------------------------------------------------------- #
# Analysis
# --------------------------------------------------------------------------- #
def analyze(info, opts):
    g = build_graph(info, opts.include_ladders)
    n = g["n"]
    areas = info["areas"]
    idx = g["idx"]; ids = g["ids"]

    R = {"file": info["file"], "version": info["version"],
         "sub_version": info["sub_version"], "is_analyzed": info["is_analyzed"],
         "per_area_trailing_bytes": info["per_area_trailing_bytes"]}

    # ---- integrity / staleness ----
    R["bsp_size_stored"] = info["bsp_size"]
    if opts.bsp and os.path.isfile(opts.bsp):
        actual = os.path.getsize(opts.bsp)
        R["bsp_size_actual"] = actual
        R["bsp_stale"] = (actual != info["bsp_size"])

    # ---- counts ----
    directed_edges = sum(len(o) for o in g["out"])
    R["area_count"] = n
    R["connection_count"] = directed_edges
    R["ladder_count"] = info.get("ladder_count", 0)
    R["hiding_spots_total"] = sum(a["hiding_spots"] for a in areas)
    R["encounter_paths_total"] = sum(a["encounters"] for a in areas)
    R["place_dict_size"] = len(info["place_names"])
    placed = sum(1 for a in areas if a["place"] != 0)
    R["place_coverage_pct"] = round(100.0 * placed / n, 1) if n else 0.0

    # ---- attribute distribution ----
    attr_counts = {name: 0 for name, _ in NAV_ATTRS}
    for a in areas:
        for name, bit in NAV_ATTRS:
            if a["attr"] & bit:
                attr_counts[name] += 1
    R["attribute_pct"] = {k: round(100.0 * v / n, 1) for k, v in attr_counts.items() if v} if n else {}
    R["jump_persisted"] = attr_counts["JUMP"]   # red flag if > 0 in finished mesh

    # ---- size / aspect ----
    dims = []
    areas_units = []
    aspects = []
    too_small = too_large = high_aspect = 0
    for a in areas:
        dx = abs(a["se"][0] - a["nw"][0])
        dy = abs(a["se"][1] - a["nw"][1])
        dims.append((dx, dy))
        areas_units.append(dx * dy)
        lo, hi = min(dx, dy), max(dx, dy)
        asp = (hi / lo) if lo > 1e-6 else float("inf")
        aspects.append(asp if asp != float("inf") else 0.0)
        if dx < opts.min_dim or dy < opts.min_dim:
            too_small += 1
        if dx > opts.max_dim or dy > opts.max_dim:
            too_large += 1
        if asp > opts.max_aspect:
            high_aspect += 1

    def stats(xs):
        if not xs:
            return {}
        xs2 = sorted(xs)
        mean = sum(xs2) / len(xs2)
        var = sum((x - mean) ** 2 for x in xs2) / len(xs2)
        return {"min": round(xs2[0], 1), "median": round(xs2[len(xs2) // 2], 1),
                "max": round(xs2[-1], 1), "mean": round(mean, 1),
                "std": round(math.sqrt(var), 1)}

    R["area_size_units"] = stats(areas_units)
    R["aspect_ratio"] = stats(aspects)
    R["too_small_areas"] = too_small
    R["too_large_areas"] = too_large
    R["high_aspect_areas"] = high_aspect

    # ---- drop / jump edge validation (approx via area z-centers) ----
    zc = {a["id"]: area_zcenter(a) for a in areas}
    unclimbable = suicide = 0
    for a in areas:
        z0 = zc[a["id"]]
        for tid in a["conns"]:
            if tid in zc:
                dz = zc[tid] - z0
                if dz > opts.max_climb:
                    unclimbable += 1
                if dz < -opts.death_drop:
                    suicide += 1
    R["unclimbable_links"] = unclimbable
    R["suicide_drop_links"] = suicide

    # ---- graph: components / islands ----
    comp, sizes = connected_components(g["und"], n)
    R["weak_components"] = len(sizes)
    main = max(range(len(sizes)), key=lambda c: sizes[c]) if sizes else 0
    main_size = sizes[main] if sizes else 0
    R["main_component_size"] = main_size
    island_nodes = [ids[v] for v in range(n) if comp[v] != main]
    R["island_area_count"] = len(island_nodes)
    R["island_area_ids_sample"] = island_nodes[:20]

    # ---- directed: SCC / one-way traps ----
    scc, nscc = tarjan_scc(g["out"], n)
    R["strongly_connected_components"] = nscc
    if n:
        from collections import Counter
        sc_sizes = Counter(scc)
        main_scc = sc_sizes.most_common(1)[0][0]
        R["main_scc_size"] = sc_sizes[main_scc]
        R["not_strongly_connected"] = n - sc_sizes[main_scc]

    # one-way edges
    oneway = sum(1 for (u, v) in g["edges"] if (v, u) not in g["edges"])
    R["one_way_connections"] = oneway

    # dead ends
    out_deg = [len(o) for o in g["out"]]
    in_deg = [0] * n
    for o in g["out"]:
        for v in o:
            in_deg[v] += 1
    R["sink_dead_ends"] = sum(1 for d in out_deg if d == 0)
    R["source_only_areas"] = sum(1 for i in range(n) if in_deg[i] == 0 and out_deg[i] > 0)
    R["isolated_areas"] = sum(1 for i in range(n) if in_deg[i] == 0 and out_deg[i] == 0)
    R["degree1_areas"] = sum(1 for u in range(n) if len(g["und"][u]) == 1)

    # ---- chokepoints ----
    ap, bridges = articulation_and_bridges(g["und"], n)
    R["articulation_points"] = len(ap)
    R["bridges"] = bridges

    if not opts.fast and n > 1:
        bt = betweenness_top(g["und"], n, comp, sizes, opts.top)
        R["betweenness_sampled"] = bool(bt and bt[0][2])
        R["betweenness_top"] = [{"area_id": ids[v], "score": round(s, 1)} for s, v, _ in bt]
        dia, avg, sm = diameter_and_avg(g["und"], n, comp, sizes)
        R["diameter"] = dia
        R["avg_path_length"] = round(avg, 2)
        R["distance_sampled"] = sm

    # ---- health score + red flags ----
    R["health_score"], R["red_flags"] = _score(R, n)
    return R


def _score(R, n):
    score = 100.0
    flags = []
    if n == 0:
        return 0, ["empty mesh"]

    if R.get("bsp_stale"):
        score -= 25; flags.append("nav is STALE vs the .bsp (stored %s != actual %s)"
                                  % (R.get("bsp_size_stored"), R.get("bsp_size_actual")))
    if R.get("is_analyzed") is False:
        score -= 10; flags.append("mesh is not analyzed (run nav_analyze)")
    if R.get("jump_persisted", 0) > 0:
        score -= 5; flags.append("%d areas still carry generation-only JUMP flag" % R["jump_persisted"])

    isl = R["island_area_count"]
    if isl:
        frac = isl / n
        score -= min(30.0, frac * 200.0)
        flags.append("%d areas (%.1f%%) are disconnected islands (unreachable from main mesh)"
                     % (isl, 100.0 * frac))
    nsc = R.get("not_strongly_connected", 0)
    if nsc:
        score -= min(10.0, (nsc / n) * 50.0)
        flags.append("%d areas are not strongly connected to the main region (possible one-way traps)" % nsc)
    if R["suicide_drop_links"]:
        score -= min(8.0, R["suicide_drop_links"] * 0.5)
        flags.append("%d connections drop > DeathDrop (%.0fu) -- fatal/one-way" % (R["suicide_drop_links"], DEATH_DROP))
    if R["unclimbable_links"]:
        score -= min(6.0, R["unclimbable_links"] * 0.5)
        flags.append("%d up-links exceed JumpCrouchHeight (%.0fu) -- likely unreachable" % (R["unclimbable_links"], JUMP_CROUCH))

    ts = R["too_small_areas"]
    if ts:
        frac = ts / n
        score -= min(8.0, frac * 40.0)
        flags.append("%d areas (%.1f%%) are sub-grid (< %.0fu) -- over-segmentation"
                     % (ts, 100.0 * frac, GENERATION_STEP))
    ha = R["high_aspect_areas"]
    if ha:
        frac = ha / n
        score -= min(6.0, frac * 30.0)
        flags.append("%d areas (%.1f%%) are long/thin (aspect ratio high) -- detour paths" % (ha, 100.0 * frac))
    if R["place_dict_size"] == 0:
        flags.append("no place names defined (poor bot callouts / bombsite detection)")

    return max(0, round(score)), flags


# --------------------------------------------------------------------------- #
# Output
# --------------------------------------------------------------------------- #
def print_report(R):
    print("=" * 64)
    print("nav graph analysis: %s" % R["file"])
    print("=" * 64)
    print("  version=%s sub_version=%s analyzed=%s trailing/area=%dB"
          % (R["version"], R["sub_version"], R["is_analyzed"], R["per_area_trailing_bytes"]))
    if "bsp_size_actual" in R:
        print("  bsp staleness: stored=%d actual=%d -> %s"
              % (R["bsp_size_stored"], R["bsp_size_actual"], "STALE" if R["bsp_stale"] else "ok"))
    print("\n  counts:")
    print("    areas=%d  connections=%d  ladders=%d" % (R["area_count"], R["connection_count"], R["ladder_count"]))
    print("    hiding_spots=%d  encounter_paths=%d" % (R["hiding_spots_total"], R["encounter_paths_total"]))
    print("    places=%d  place_coverage=%.1f%%" % (R["place_dict_size"], R["place_coverage_pct"]))
    print("\n  attributes (%% of areas): " +
          (", ".join("%s=%.1f" % (k, v) for k, v in sorted(R["attribute_pct"].items(), key=lambda x: -x[1])) or "(none)"))
    print("\n  geometry:")
    print("    area_size(units^2): %s" % R["area_size_units"])
    print("    aspect_ratio:       %s" % R["aspect_ratio"])
    print("    too_small=%d  too_large=%d  high_aspect=%d"
          % (R["too_small_areas"], R["too_large_areas"], R["high_aspect_areas"]))
    print("    unclimbable_up_links=%d  suicide_drop_links=%d" % (R["unclimbable_links"], R["suicide_drop_links"]))
    print("\n  graph / connectivity:")
    print("    weak_components=%d  main_component=%d  islands=%d"
          % (R["weak_components"], R["main_component_size"], R["island_area_count"]))
    if R["island_area_ids_sample"]:
        print("      island ids (sample): %s" % R["island_area_ids_sample"])
    print("    SCCs=%d  main_scc=%d  not_strongly_connected=%d"
          % (R["strongly_connected_components"], R.get("main_scc_size", 0), R.get("not_strongly_connected", 0)))
    print("    one_way_connections=%d" % R["one_way_connections"])
    print("    dead_ends(sink)=%d  source_only=%d  isolated=%d  degree1=%d"
          % (R["sink_dead_ends"], R["source_only_areas"], R["isolated_areas"], R["degree1_areas"]))
    print("    articulation_points=%d  bridges=%d  (structural chokepoints)"
          % (R["articulation_points"], R["bridges"]))
    if "diameter" in R:
        print("    diameter=%d  avg_path_len=%.2f%s"
              % (R["diameter"], R["avg_path_length"], "  (sampled)" if R.get("distance_sampled") else ""))
    if "betweenness_top" in R:
        top = ", ".join("#%d(%.0f)" % (e["area_id"], e["score"]) for e in R["betweenness_top"][:8])
        print("    top chokepoints by betweenness%s: %s"
              % (" (sampled)" if R.get("betweenness_sampled") else "", top))
    print("\n  HEALTH SCORE: %d/100" % R["health_score"])
    if R["red_flags"]:
        print("  red flags:")
        for f in R["red_flags"]:
            print("    - " + f)
    else:
        print("  no red flags.")
    print()


def do_compare(a_path, b_path, opts):
    ia, ib = parse_nav(a_path), parse_nav(b_path)
    Ra = analyze(ia, _opts_for(opts, opts.bsp_a))
    Rb = analyze(ib, _opts_for(opts, opts.bsp_b))
    keys = ["area_count", "connection_count", "ladder_count", "hiding_spots_total",
            "place_coverage_pct", "weak_components", "island_area_count",
            "one_way_connections", "articulation_points", "bridges",
            "too_small_areas", "high_aspect_areas", "suicide_drop_links",
            "diameter", "avg_path_length", "health_score"]
    print("%-26s %-16s %-16s %s" % ("metric", os.path.basename(a_path), os.path.basename(b_path), ""))
    print("-" * 74)
    for k in keys:
        va, vb = Ra.get(k), Rb.get(k)
        mark = "" if va == vb else "  <-- differs"
        print("%-26s %-16s %-16s%s" % (k, va, vb, mark))
    if Ra.get("area_count") and Rb.get("area_count"):
        d = abs(Ra["area_count"] - Rb["area_count"])
        print("-" * 74)
        print("area_count delta: %d (%.1f%%)" % (d, 100.0 * d / max(Ra["area_count"], Rb["area_count"])))
    return 0


class _O:  # lightweight opts holder for compare sub-analyses
    pass


def _opts_for(opts, bsp):
    o = _O()
    for k in ("top", "include_ladders", "min_dim", "max_dim", "max_aspect",
              "max_climb", "death_drop", "fast"):
        setattr(o, k, getattr(opts, k))
    o.bsp = bsp
    return o


def main(argv):
    p = argparse.ArgumentParser(description="Offline Source .nav quality/graph analyzer")
    p.add_argument("nav", nargs="?")
    p.add_argument("--json", action="store_true")
    p.add_argument("--bsp", default=None, help="source .bsp for staleness check")
    p.add_argument("--top", type=int, default=10, help="top-N chokepoints by betweenness")
    p.add_argument("--include-ladders", action="store_true")
    p.add_argument("--min-dim", type=float, default=GENERATION_STEP)
    p.add_argument("--max-dim", type=float, default=1024.0)
    p.add_argument("--max-aspect", type=float, default=10.0)
    p.add_argument("--max-climb", type=float, default=JUMP_CROUCH)
    p.add_argument("--death-drop", type=float, default=DEATH_DROP)
    p.add_argument("--fast", action="store_true", help="skip betweenness/diameter (big meshes)")
    p.add_argument("--compare", nargs=2, metavar=("A", "B"))
    p.add_argument("--bsp-a", default=None)
    p.add_argument("--bsp-b", default=None)
    opts = p.parse_args(argv[1:])

    if opts.compare:
        return do_compare(opts.compare[0], opts.compare[1], opts)
    if not opts.nav:
        p.print_help(); return 2
    try:
        info = parse_nav(opts.nav)
    except NavParseError as e:
        print("error: %s" % e, file=sys.stderr); return 1
    R = analyze(info, opts)
    if opts.json:
        import json
        print(json.dumps(R, indent=2))
    else:
        print_report(R)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
