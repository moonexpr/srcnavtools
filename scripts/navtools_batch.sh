#!/usr/bin/env bash
#
# navtools_batch.sh - multicore batch nav-mesh generation.
#
# Source's nav builder is single-threaded by design (nav_generate.cpp forces
# host_thread_mode 0 -- "need non-threaded server for light calcs"), so the way
# to use many cores is to generate many maps AT ONCE: this driver runs up to -j
# independent navtools_create_mesh processes in parallel, one engine instance
# per map, each on its own UDP port. Ideal for regenerating a dedicated
# server's whole map rotation.
#
# Usage:
#   navtools_batch.sh -basedir <SDKBase> -game <mod> -j <N> [options] <map...>
#   navtools_batch.sh -basedir <SDKBase> -game <mod> -maps <file>
#   navtools_batch.sh -basedir <SDKBase> -game <mod> -dir <bspdir>
#
# Options:
#   -basedir <dir>   engine base dir (contains bin/engine.so)        [required]
#   -game <dir>      mod dir under basedir with gameinfo.txt          [required]
#   -j <N>           max concurrent jobs (default: nproc)
#   -tool <path>     navtools_create_mesh binary (default: build/navtools_create_mesh)
#   -maps <file>     newline-separated map names (first token per line)
#   -dir <dir>       install every *.bsp from <dir> into <game>/maps and build it
#   -logs <dir>      per-map log directory (default: ./batchlogs)
#   -baseport <p>    first UDP port; job i uses baseport+i (default: 27100)
#   -- <args...>     extra args passed through to navtools_create_mesh
#
set -euo pipefail
REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

TOOL="$REPO_ROOT/build/navtools_create_mesh"
BASEDIR=""; GAME=""; JOBS="$(nproc 2>/dev/null || echo 4)"
MAPS_FILE=""; BSP_DIR=""; LOGDIR="$PWD/batchlogs"; BASEPORT=27100
EXTRA=()
MAPS=()

while [[ $# -gt 0 ]]; do
	case "$1" in
		-basedir) BASEDIR="$2"; shift 2;;
		-game)    GAME="$2"; shift 2;;
		-j)       JOBS="$2"; shift 2;;
		-tool)    TOOL="$2"; shift 2;;
		-maps)    MAPS_FILE="$2"; shift 2;;
		-dir)     BSP_DIR="$2"; shift 2;;
		-logs)    LOGDIR="$2"; shift 2;;
		-baseport) BASEPORT="$2"; shift 2;;
		--)       shift; EXTRA=("$@"); break;;
		-h|--help) grep '^#' "$0" | sed 's/^# \{0,1\}//'; exit 0;;
		-*)       echo "unknown option: $1" >&2; exit 2;;
		*)        MAPS+=("$1"); shift;;
	esac
done

[[ -n "$BASEDIR" ]] || { echo "error: -basedir required" >&2; exit 2; }
[[ -n "$GAME" ]]    || { echo "error: -game required" >&2; exit 2; }
[[ -x "$TOOL" ]]    || { echo "error: tool not found/executable: $TOOL" >&2; exit 2; }

# Collect map list from -dir / -maps / positionals.
if [[ -n "$BSP_DIR" ]]; then
	mkdir -p "$BASEDIR/$GAME/maps"
	for b in "$BSP_DIR"/*.bsp; do
		[[ -e "$b" ]] || continue
		cp -f "$b" "$BASEDIR/$GAME/maps/"
		MAPS+=("$(basename "$b" .bsp)")
	done
fi
if [[ -n "$MAPS_FILE" ]]; then
	while read -r m _; do
		[[ -z "$m" || "$m" == \#* ]] && continue
		MAPS+=("$m")
	done < "$MAPS_FILE"
fi
[[ ${#MAPS[@]} -gt 0 ]] || { echo "error: no maps given (use <map...>, -maps, or -dir)" >&2; exit 2; }

mkdir -p "$LOGDIR"
echo "navtools_batch: ${#MAPS[@]} maps, -j $JOBS, tool=$TOOL"
echo "  basedir=$BASEDIR game=$GAME logs=$LOGDIR"

start_all=$(date +%s)
declare -A PID2MAP STATUS
idx=0; fail=0

run_one() {  # $1 = map, $2 = port
	local map="$1" port="$2" t0 t1 rc
	t0=$(date +%s)
	if "$TOOL" -basedir "$BASEDIR" -game "$GAME" -map "$map" -port "$port" \
			"${EXTRA[@]}" >"$LOGDIR/$map.log" 2>&1; then rc=0; else rc=$?; fi
	t1=$(date +%s)
	echo "$rc $((t1 - t0))" > "$LOGDIR/$map.status"
}

# Simple job pool: keep at most $JOBS background jobs alive.
for map in "${MAPS[@]}"; do
	while [[ "$(jobs -rp | wc -l)" -ge "$JOBS" ]]; do wait -n 2>/dev/null || true; done
	port=$((BASEPORT + idx)); idx=$((idx + 1))
	echo "  [start] $map (port $port)"
	run_one "$map" "$port" &
done
wait

# Summarize.
end_all=$(date +%s)
echo
printf "%-28s %-8s %s\n" "MAP" "RESULT" "SECONDS"
printf '%.0s-' {1..48}; echo
for map in "${MAPS[@]}"; do
	if [[ -f "$LOGDIR/$map.status" ]]; then
		read -r rc secs < "$LOGDIR/$map.status"
	else
		rc=99; secs="?"
	fi
	if [[ "$rc" == "0" && -f "$BASEDIR/$GAME/maps/$map.nav" ]]; then
		printf "%-28s %-8s %s\n" "$map" "OK" "$secs"
	else
		printf "%-28s %-8s %s (see %s)\n" "$map" "FAIL($rc)" "$secs" "$LOGDIR/$map.log"
		fail=$((fail + 1))
	fi
done
echo
echo "done: $((${#MAPS[@]} - fail))/${#MAPS[@]} ok, ${fail} failed, $((end_all - start_all))s wallclock on -j $JOBS"
[[ "$fail" -eq 0 ]]
