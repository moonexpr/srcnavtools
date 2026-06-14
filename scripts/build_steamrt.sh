#!/usr/bin/env bash
#
# build_steamrt.sh - canonical build, against the Steam Runtime.
#
# Wires navtools_create_mesh into the vendored SDK solution and builds it the
# way the SDK builds its own projects: with VPC + ninja inside the SteamRT
# "sniper" container.
#
# Requirements (per the SDK README):
#   * Source SDK 2013 Multiplayer installed via Steam
#   * podman
#
# The build runs ./buildallprojects, which pulls the SteamRT sniper image and
# compiles everything (including our project) against the runtime. The output
# binary lands in external/source-sdk-2013/game/bin/.
#
set -euo pipefail
REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SDK_SRC="$REPO_ROOT/external/source-sdk-2013/src"

command -v podman >/dev/null 2>&1 || {
	echo "error: podman is required for the Steam Runtime build."
	echo "       For a build without podman, use the Makefile: make"
	exit 1
}

echo "==> integrating project into the SDK solution"
bash "$REPO_ROOT/scripts/integrate_sdk.sh"

echo "==> building all SDK projects against the Steam Runtime (this is slow the first time)"
cd "$SDK_SRC"
./buildallprojects "$@"

echo
echo "Done. Binary: external/source-sdk-2013/game/bin/navtools_create_mesh"
