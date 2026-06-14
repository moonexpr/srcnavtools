#!/usr/bin/env bash
#
# setup.sh - one-shot setup for the direct (non-container) build.
#
# Initializes the vendored Source SDK 2013 submodule and builds
# navtools_create_mesh with the standalone Makefile (compiles tier1 + mathlib
# from the SDK sources; links against the SDK's prebuilt libtier0/libvstdlib/
# appframework). This does NOT need podman or the Steam Runtime container.
#
set -euo pipefail
REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO_ROOT"

echo "==> initializing the Source SDK 2013 submodule"
git submodule update --init --recursive

echo "==> building navtools_create_mesh"
make -j"$(nproc)"

echo
echo "Built: $REPO_ROOT/build/navtools_create_mesh"
echo "Next: generate a mesh (needs a Source SDK Base 2013 MP runtime):"
echo "  ./build/navtools_create_mesh -basedir <SDKBase> -game <mod> -map <map>"
