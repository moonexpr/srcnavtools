#!/usr/bin/env bash
#
# integrate_sdk.sh - wire navtools_create_mesh into the vendored Source SDK 2013
# VPC solution so the canonical container build (./buildallprojects) picks it up.
#
# The SDK is a git submodule, so its working-tree contents are not tracked by
# this repo. This script (re)applies our project into that tree:
#   1. copies src/navtools_create_mesh into <sdk>/src/navtools_create_mesh
#   2. registers the project in vpc_scripts/projects.vgc
#   3. adds it to the "everything" group in vpc_scripts/groups.vgc
#
# It is idempotent: running it again is a no-op beyond refreshing the copy.
#
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SDK_SRC="$REPO_ROOT/external/source-sdk-2013/src"
PROJ_SRC="$REPO_ROOT/src/navtools_create_mesh"
PROJ_DST="$SDK_SRC/navtools_create_mesh"

if [[ ! -d "$SDK_SRC/vpc_scripts" ]]; then
	echo "error: SDK not found at $SDK_SRC"
	echo "       run: git submodule update --init --recursive"
	exit 1
fi

echo "==> copying project into SDK tree: $PROJ_DST"
mkdir -p "$PROJ_DST"
cp -f "$PROJ_SRC"/*.cpp "$PROJ_SRC"/*.h "$PROJ_SRC"/*.vpc "$PROJ_DST"/

echo "==> registering project in projects.vgc"
python3 - "$SDK_SRC/vpc_scripts/projects.vgc" <<'PY'
import sys
path = sys.argv[1]
text = open(path).read()
if '$Project "navtools_create_mesh"' in text:
    print("    already registered")
else:
    block = (
        '\n$Project "navtools_create_mesh"\n'
        '{\n'
        '\t"navtools_create_mesh\\navtools_create_mesh.vpc"\n'
        '}\n'
    )
    # Append at end of file; VPC does not care about ordering.
    with open(path, "a") as f:
        f.write(block)
    print("    added $Project navtools_create_mesh")
PY

echo "==> adding project to the \"everything\" group in groups.vgc"
python3 - "$SDK_SRC/vpc_scripts/groups.vgc" <<'PY'
import re, sys
path = sys.argv[1]
text = open(path).read()
if re.search(r'"navtools_create_mesh"', text):
    print("    already in a group")
else:
    # Insert into the $Group "everything" { ... } block.
    m = re.search(r'(\$Group\s+"everything"\s*\{)', text)
    if not m:
        print("    WARNING: could not find $Group \"everything\"; add it manually")
        sys.exit(0)
    insert_at = m.end()
    text = text[:insert_at] + '\n\t"navtools_create_mesh"' + text[insert_at:]
    open(path, "w").write(text)
    print("    added to everything group")
PY

echo
echo "Integration complete. Build with the Steam Runtime container:"
echo "    cd external/source-sdk-2013/src && ./buildallprojects"
echo "The resulting binary is written to external/source-sdk-2013/game/bin/."
