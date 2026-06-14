#!/usr/bin/env bash
#
# run.sh - convenience wrapper around navtools_create_mesh.
#
# Usage:
#   scripts/run.sh <basedir> <game> <map> [extra args...]
#
# <basedir>  Source SDK Base 2013 Multiplayer dir (contains bin/engine.so)
# <game>     mod dir under <basedir> with gameinfo.txt (e.g. tf, hl2mp)
# <map>      map name without .bsp (must exist under <basedir>/<game>/maps)
#
# Example:
#   scripts/run.sh ~/sdkbase tf cp_orange_x3
#
set -euo pipefail
REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TOOL="$REPO_ROOT/build/navtools_create_mesh"

if [[ $# -lt 3 ]]; then
	grep '^#' "$0" | sed 's/^# \{0,1\}//'
	exit 2
fi
[[ -x "$TOOL" ]] || { echo "error: build first (make or scripts/setup.sh)"; exit 2; }

basedir="$1"; game="$2"; map="$3"; shift 3
exec "$TOOL" -basedir "$basedir" -game "$game" -map "$map" "$@"
