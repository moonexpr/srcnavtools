# srcnavtools

Standalone tools for working with Source Engine navigation meshes, built
against the [Source SDK 2013](https://github.com/ValveSoftware/source-sdk-2013)
libraries.

The first tool is **`navtools_create_mesh`**: a headless command-line program
that generates a navigation mesh (`.nav`) for a map *without loading the whole
game* — no client, no renderer, no sound. It links the SDK's tier/appframework
libraries, loads only the dedicated engine at runtime, drives the stock
`nav_generate` console command, and writes the `.nav` to disk.

The generation algorithm itself is the unmodified Valve code in
[`game/server/nav_generate.cpp`](https://github.com/ValveSoftware/source-sdk-2013/blob/master/src/game/server/nav_generate.cpp)
— this tool is the *host* that runs it headlessly.

---

## How it works

Nav generation lives inside the game's `server` module and only needs one
engine capability to do its job: **hull/line traces against the map's
collision** (`UTIL_TraceHull`/`UTIL_TraceLine` → `IEngineTrace`, backed by the
BSP brushes + vphysics + static props). Everything else it touches
(`CBaseEntity`, ladders, `gEntList`) comes from the server module the engine
loads when a map is spawned.

So instead of re-implementing collision, `navtools_create_mesh` boots a
**headless dedicated engine** and steers it:

```
 navtools_create_mesh (this tool)
   ├── links:  tier0, tier1, vstdlib, mathlib, appframework   (SDK libs)
   ├── dlopen: filesystem_stdio.so, libvstdlib.so, engine.so  (at runtime)
   └── drives: IDedicatedServerAPI (VENGINE_HLDS_API_VERSION002)
                 ModInit() → RunFrame() loop → AddConsoleText()
                   "map <name>"        # engine spawns world collision only
                   "sv_cheats 1"       # nav_generate is FCVAR_CHEAT
                   "nav_generate"      # CNavMesh::BeginGeneration()
                 ← watch console spew for "Generation complete! ... saved."
                   "quit"
```

The client, renderer, material system and sound are never loaded. See
[docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) for the full design and the exact
SDK symbols involved.

---

## Requirements

- Linux, 64-bit, `g++` (tested with g++ 13).
- The vendored Source SDK 2013 submodule (`external/source-sdk-2013`).
- **To actually generate a mesh:** a working *Source SDK Base 2013
  Multiplayer* runtime installed via Steam — i.e. the `bin/engine.so`,
  `bin/filesystem_stdio.so` and the mod's `server` module plus its content
  (`gameinfo.txt`, maps, materials/models the map references). This is the same
  runtime the game uses; the tool drives it, it does not replace it.

Two build paths are provided:

| Path | Command | Needs podman? | Notes |
|------|---------|---------------|-------|
| **Direct** (Makefile) | `make` | no | compiles `tier1`/`mathlib` from the SDK sources, links the prebuilt `libtier0.so`/`libvstdlib.so`/`appframework.a`. Fast, CI-friendly. |
| **Canonical** (Steam Runtime) | `scripts/build_steamrt.sh` | yes | wires the project into the SDK's VPC solution and builds it via `./buildallprojects` inside the SteamRT *sniper* container — required for binaries distributed on Steam. |

---

## Build

```bash
git clone --recurse-submodules <this repo>
cd srcnavtools

# Direct build (no container):
./scripts/setup.sh          # = git submodule update --init && make
# -> build/navtools_create_mesh
```

Or the canonical Steam-Runtime build:

```bash
./scripts/build_steamrt.sh  # needs podman; output in external/source-sdk-2013/game/bin
```

## Usage

```
navtools_create_mesh -game <moddir> -map <mapname> [options]

Required:
  -game <dir>      mod/game directory containing gameinfo.txt
  -map  <name>     map name without .bsp (under <game>/maps)

Options:
  -basedir <dir>   engine base dir (contains bin/engine.so).
                   Defaults to $SOURCE_SDK_BASE, else the current dir.
  -engine <name>   engine module name (default: engine)
  -incremental     use nav_generate_incremental
  -timeout <sec>   abort if not finished (default 1800)
  -settle <n>      frames to wait after "map" before generating (default 120)
  -v               echo all engine console output
```

Example:

```bash
./build/navtools_create_mesh \
  -basedir ~/.steam/steam/steamapps/common/"Source SDK Base 2013 Multiplayer" \
  -game tf -map cp_orange_x3
# writes <basedir>/tf/maps/cp_orange_x3.nav
```

---

## Testing

`.nav` files have a fixed header (`magic 0xFEEDFACE`, version, sub-version, the
source BSP size, then a place directory and the area count). `scripts/nav_inspect.py`
parses it and can compare two meshes:

```bash
python3 scripts/nav_inspect.py test/reference/cp_orange_x3.nav
python3 scripts/nav_inspect.py --compare generated.nav test/reference/cp_orange_x3.nav
```

`scripts/test_maps.sh` is the end-to-end harness: it downloads maps from
[danyisill/tf2-maps](https://github.com/danyisill/tf2-maps), generates a mesh
for each, and compares against the upstream reference `.nav` where one exists
(only 4 maps in that repo ship a reference nav; the rest are BSP-only and are
self-validated):

```bash
./scripts/test_maps.sh -basedir <SDKBase> -game tf
```

Committed reference meshes and their parsed properties (verified with
`nav_inspect.py`):

| map | version | sub_version | bsp_size | areas |
|-----|---------|-------------|----------|-------|
| cp_orange_x3   | 16 | 2 | 3,290,104  | 367  |
| cp_orange_z4_v3| 16 | 2 | 5,191,162  | 813  |
| mvm_mountain_b3| 16 | 2 | 3,369,492  | 1200 |
| vsh_crevice_b2 | 16 | 2 | 38,134,612 | 2287 |

> **Note on what has been verified here:** the tool **compiles, links and runs**
> against the real SDK (it drives the SDK's AppFramework module loader and only
> stops where the Steam-distributed `engine.so` would be loaded), and the
> `.nav` inspector/compare logic is validated against the four real reference
> meshes above. Running full generation across maps requires the Steam runtime
> + per-game content (e.g. TF2), which is not present in every environment; run
> `scripts/test_maps.sh` where that runtime is installed to produce the
> generated-vs-reference comparison table.

---

## Layout

```
src/navtools_create_mesh/   the tool (main, app/engine host, spew hook, options, VPC)
scripts/                    setup, run, integrate, steamrt build, test harness, nav_inspect.py
test/reference/             committed upstream .nav golden files
test/maps.txt               map list for the harness
external/source-sdk-2013/   vendored SDK (git submodule)
Makefile                    direct (non-container) build
```
