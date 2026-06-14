#!/usr/bin/env python3
"""
nav_inspect.py - inspect / compare Source Engine navigation mesh (.nav) files.

Parses the .nav header exactly as written by CNavMesh::Save() in
source-sdk-2013/src/game/server/nav_file.cpp:

    uint32   magic            (0xFEEDFACE)
    uint32   version          (16 = "NavCurrentVersion")
    uint32   subVersion       (game-specific; owned by the derived CNavMesh)
    uint32   bspSize          (size in bytes of the .bsp the nav was built from)
    uint8    isAnalyzed       (version >= 14)
    -- PlaceDirectory::Save --
    uint16   placeCount
      placeCount * { uint16 len; char[len] name }
    uint8    hasUnnamedAreas  (version >= 12)
    -- SaveCustomDataPreArea() (empty for the base mesh) --
    uint32   areaCount        (number of nav areas)
    ...

The fixed header is parsed reliably for any game.  areaCount is best-effort: if
a derived mesh wrote game-specific data in SaveCustomDataPreArea() the value is
flagged as unreliable rather than trusted blindly.

Usage:
    nav_inspect.py <file.nav>                 # print fields as key=value
    nav_inspect.py --json <file.nav>          # print fields as JSON
    nav_inspect.py --compare A.nav B.nav      # compare two navs, exit !=0 if they
                                              # differ beyond tolerance
"""
import json
import struct
import sys

NAV_MAGIC = 0xFEEDFACE


class NavParseError(Exception):
    pass


def _u8(b, o):  return b[o], o + 1
def _u16(b, o): return struct.unpack_from("<H", b, o)[0], o + 2
def _u32(b, o): return struct.unpack_from("<I", b, o)[0], o + 4


def parse_nav(path):
    with open(path, "rb") as f:
        b = f.read()

    info = {"file": path, "filesize": len(b)}
    o = 0
    if len(b) < 16:
        raise NavParseError("file too small to be a .nav")

    magic, o = _u32(b, o)
    info["magic"] = "0x%08X" % magic
    info["valid_magic"] = (magic == NAV_MAGIC)
    if magic != NAV_MAGIC:
        raise NavParseError("bad magic 0x%08X (expected 0x%08X)" % (magic, NAV_MAGIC))

    info["version"], o = _u32(b, o)
    info["sub_version"], o = _u32(b, o)
    info["bsp_size"], o = _u32(b, o)

    if info["version"] >= 14:
        analyzed, o = _u8(b, o)
        info["is_analyzed"] = bool(analyzed)

    # PlaceDirectory
    place_count, o = _u16(b, o)
    info["place_count"] = place_count
    try:
        for _ in range(place_count):
            slen, o = _u16(b, o)
            o += slen
        if info["version"] >= 12:
            has_unnamed, o = _u8(b, o)
            info["has_unnamed_areas"] = bool(has_unnamed)

        # SaveCustomDataPreArea() is empty for the base CNavMesh. If present
        # (some derived game meshes), the area count below will look wrong.
        area_count, o = _u32(b, o)
        # Sanity: a real area count can't exceed (remaining bytes / a few bytes
        # per area). If it does, custom pre-area data almost certainly exists.
        plausible = 0 <= area_count <= (len(b) - o + 4)
        info["area_count"] = area_count
        info["area_count_reliable"] = bool(plausible)
    except (struct.error, IndexError):
        info["area_count"] = None
        info["area_count_reliable"] = False

    return info


def print_kv(info):
    for k in ("file", "filesize", "magic", "valid_magic", "version", "sub_version",
              "bsp_size", "is_analyzed", "place_count", "has_unnamed_areas",
              "area_count", "area_count_reliable"):
        if k in info:
            print("%-20s = %s" % (k, info[k]))


def compare(a_path, b_path):
    a = parse_nav(a_path)
    b = parse_nav(b_path)

    print("field                 %-24s %-24s" % (a_path, b_path))
    print("-" * 78)
    same = True
    keys = ["magic", "version", "sub_version", "bsp_size", "is_analyzed",
            "place_count", "area_count"]
    for k in keys:
        av, bv = a.get(k), b.get(k)
        mark = "" if av == bv else "   <-- differs"
        if av != bv:
            same = False
        print("%-20s  %-24s %-24s%s" % (k, av, bv, mark))

    # An identical map should produce the same format version and the same area
    # count (generation is deterministic for a given engine build + bsp).
    ac_a, ac_b = a.get("area_count"), b.get("area_count")
    if a.get("area_count_reliable") and b.get("area_count_reliable") and ac_a and ac_b:
        diff = abs(ac_a - ac_b)
        pct = 100.0 * diff / max(ac_a, ac_b)
        print("-" * 78)
        print("area_count delta: %d (%.2f%%)" % (diff, pct))

    return 0 if same else 1


def main(argv):
    if len(argv) >= 2 and argv[1] == "--compare" and len(argv) == 4:
        return compare(argv[2], argv[3])

    as_json = False
    args = argv[1:]
    if args and args[0] == "--json":
        as_json = True
        args = args[1:]
    if len(args) != 1:
        print(__doc__)
        return 2

    try:
        info = parse_nav(args[0])
    except NavParseError as e:
        print("error: %s" % e, file=sys.stderr)
        return 1

    if as_json:
        print(json.dumps(info, indent=2))
    else:
        print_kv(info)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
