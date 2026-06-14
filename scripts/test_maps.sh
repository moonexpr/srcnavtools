#!/usr/bin/env bash
#
# test_maps.sh - end-to-end test harness for navtools_create_mesh.
#
# For each map in test/maps.txt:
#   1. download <map>.bsp from the tf2-maps repo (cached under test/work/)
#   2. install it into <game>/maps/
#   3. run navtools_create_mesh to generate <map>.nav
#   4. inspect the result and, when an upstream reference nav exists under
#      test/reference/, compare area counts / format
#
# Generation REQUIRES a working Source SDK Base 2013 Multiplayer runtime
# (engine.so, filesystem_stdio.so and the game's server module with the nav
# code) -- the same thing the game needs to run. Point -basedir at it.
#
# Usage:
#   scripts/test_maps.sh -basedir <SDKBaseDir> -game <moddir> [-maps <file>]
#                        [-tool <path>] [-only <map>]
#
# Example:
#   scripts/test_maps.sh \
#       -basedir ~/.steam/steam/steamapps/common/"Source SDK Base 2013 Multiplayer" \
#       -game tf
#
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
RAW_BASE="https://github.com/danyisill/tf2-maps/raw/refs/heads/master"

TOOL="$REPO_ROOT/build/navtools_create_mesh"
MAPS_FILE="$REPO_ROOT/test/maps.txt"
WORK="$REPO_ROOT/test/work"
REF_DIR="$REPO_ROOT/test/reference"
INSPECT="$REPO_ROOT/scripts/nav_inspect.py"
BASEDIR=""
GAME=""
ONLY=""

while [[ $# -gt 0 ]]; do
	case "$1" in
		-basedir) BASEDIR="$2"; shift 2;;
		-game)    GAME="$2"; shift 2;;
		-maps)    MAPS_FILE="$2"; shift 2;;
		-tool)    TOOL="$2"; shift 2;;
		-only)    ONLY="$2"; shift 2;;
		-h|--help) grep '^#' "$0" | sed 's/^# \{0,1\}//'; exit 0;;
		*) echo "unknown arg: $1"; exit 2;;
	esac
done

[[ -n "$BASEDIR" ]] || { echo "error: -basedir is required"; exit 2; }
[[ -n "$GAME" ]]    || { echo "error: -game is required"; exit 2; }
[[ -x "$TOOL" ]]    || { echo "error: tool not found/executable: $TOOL (run make)"; exit 2; }

mkdir -p "$WORK"
MAPS_DIR="$BASEDIR/$GAME/maps"
mkdir -p "$MAPS_DIR"

pass=0; fail=0; skip=0
printf "\n%-22s %-10s %-10s %-10s %s\n" "MAP" "REF_AREAS" "GEN_AREAS" "DELTA%" "RESULT"
printf '%.0s-' {1..70}; echo

while read -r map hasref _; do
	[[ -z "$map" || "$map" == \#* ]] && continue
	[[ -n "$ONLY" && "$map" != "$ONLY" ]] && continue

	bsp="$WORK/$map.bsp"
	if [[ ! -f "$bsp" ]]; then
		curl -sL "$RAW_BASE/$map.bsp" -o "$bsp" || { echo "$map: download failed"; fail=$((fail+1)); continue; }
	fi
	cp -f "$bsp" "$MAPS_DIR/$map.bsp"
	rm -f "$MAPS_DIR/$map.nav"

	# Generate.
	if ! "$TOOL" -basedir "$BASEDIR" -game "$GAME" -map "$map" >"$WORK/$map.log" 2>&1; then
		printf "%-22s %-10s %-10s %-10s %s\n" "$map" "-" "-" "-" "GEN-FAIL (see $WORK/$map.log)"
		fail=$((fail+1)); continue
	fi

	gen="$MAPS_DIR/$map.nav"
	[[ -f "$gen" ]] || { printf "%-22s %-10s %-10s %-10s %s\n" "$map" "-" "-" "-" "NO-OUTPUT"; fail=$((fail+1)); continue; }
	cp -f "$gen" "$WORK/$map.gen.nav"

	gen_areas="$(python3 "$INSPECT" --json "$gen" | python3 -c 'import json,sys;print(json.load(sys.stdin).get("area_count"))')"

	if [[ "$hasref" == "yes" && -f "$REF_DIR/$map.nav" ]]; then
		ref_areas="$(python3 "$INSPECT" --json "$REF_DIR/$map.nav" | python3 -c 'import json,sys;print(json.load(sys.stdin).get("area_count"))')"
		# "similar" = within 15% area-count of the reference (different engine
		# builds / map versions vary slightly; topology should be close).
		delta="$(python3 -c "a=$ref_areas;b=$gen_areas;print('%.1f'%(100.0*abs(a-b)/max(a,b)) if a and b else 'NA')")"
		ok="$(python3 -c "print('PASS' if $gen_areas and abs($ref_areas-$gen_areas)<=0.15*max($ref_areas,$gen_areas) else 'DIFF')")"
		printf "%-22s %-10s %-10s %-10s %s\n" "$map" "$ref_areas" "$gen_areas" "$delta" "$ok"
		[[ "$ok" == "PASS" ]] && pass=$((pass+1)) || fail=$((fail+1))
	else
		# No reference: self-validate (must be a valid nav with areas).
		if [[ -n "$gen_areas" && "$gen_areas" != "None" && "$gen_areas" -gt 0 ]]; then
			printf "%-22s %-10s %-10s %-10s %s\n" "$map" "n/a" "$gen_areas" "-" "OK(self)"
			pass=$((pass+1))
		else
			printf "%-22s %-10s %-10s %-10s %s\n" "$map" "n/a" "${gen_areas:-0}" "-" "INVALID"
			fail=$((fail+1))
		fi
	fi
done < "$MAPS_FILE"

echo
echo "pass=$pass fail=$fail skip=$skip"
[[ "$fail" -eq 0 ]]
