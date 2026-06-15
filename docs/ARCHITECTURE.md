# navtools_create_mesh — architecture

This document explains how the tool generates a Source navigation mesh without
loading the full game, and records the exact Source SDK 2013 symbols it relies
on (commit
[`c98767b`](https://github.com/ValveSoftware/source-sdk-2013/tree/c98767b329f07c086281d787cc0e1c4d9a6b1410)).

## The problem

`nav_generate` is a server console command. The generation code
(`game/server/nav_generate.cpp`, `game/server/nav_mesh.cpp`) is compiled into
the game's **server module** and is wired deep into the server object graph
(`cbase.h`, `CBaseEntity`, `gEntList`, ladder entities). The *only* engine
service it needs to sample the world is tracing:

```cpp
// nav_generate.cpp
UTIL_TraceHull( *start, pos, NavTraceMins, NavTraceMaxs,
                TheNavMesh->GetGenerationTraceMask(), traceIgnore,
                COLLISION_GROUP_NONE, &result );
```

`UTIL_TraceHull`/`UTIL_TraceLine` resolve to `IEngineTrace`, which the engine
backs with the BSP brush model, vphysics collision and static props — all set
up when a map is spawned.

Re-implementing that collision pipeline standalone is a large undertaking. The
pragmatic alternative (and the chosen design) is to **run the real engine
headlessly** and only skip the parts a mesh build does not need: rendering,
sound, the client, networking with real players.

## The approach: a headless dedicated host

The engine already supports running without a window or client — that is what a
dedicated server is. It exposes an embedding interface,
`IDedicatedServerAPI` (`public/engine_hlds_api.h`,
version string `VENGINE_HLDS_API_VERSION002`):

```cpp
class IDedicatedServerAPI : public IAppSystem
{
    virtual bool ModInit( ModInfo_t &info ) = 0;
    virtual void ModShutdown( void ) = 0;
    virtual bool RunFrame( void ) = 0;            // false => engine is quitting
    virtual void AddConsoleText( char *text ) = 0;
    virtual void UpdateStatus( float*, int*, int*, char*, int ) = 0;
    virtual void UpdateHostname( char*, int ) = 0;
};

struct ModInfo_t {
    void              *m_pInstance;
    const char        *m_pBaseDirectory;
    const char        *m_pInitialMod;
    const char        *m_pInitialGame;
    CAppSystemGroup   *m_pParentAppSystemGroup;
    bool               m_bTextMode;
};
```

The tool is a small `CAppSystemGroup` (`public/appframework/IAppSystemGroup.h`)
that:

1. **`Create()`** — `LoadModule()` + `AddSystem()` for exactly three modules
   from `<basedir>/bin/`:
   - `filesystem_stdio` → `IFileSystem` (`FILESYSTEM_INTERFACE_VERSION`,
     `"VFileSystem022"`)
   - `libvstdlib` → `ICvar` (`CVAR_INTERFACE_VERSION`, `"VEngineCvar004"`)
   - `engine` → `IDedicatedServerAPI` (`VENGINE_HLDS_API_VERSION002`)

   The client, renderer, material system and sound modules are never touched.
2. **`PreInit()`** — adds the base directory to the filesystem search paths so
   the engine can locate `bin/` and the mod's `gameinfo.txt`.
3. **`Main()`** — fills in `ModInfo_t` (text mode, `m_pParentAppSystemGroup =
   this`), calls `ModInit()`, then runs the generation driver and `ModShutdown()`.

The framework's `Run()` performs the standard
`Create → Connect → PreInit → Init → Main → … → Destroy` lifecycle; the engine
module is connected/initialized like any other `IAppSystem` before `Main()`.

## The generation driver

Inside `Main()` the driver pumps `RunFrame()` and feeds the console:

```
AddConsoleText("map <name>\n")        // engine spawns the world (collision)
... wait `settleFrames` frames ...    // let the level finish spawning
AddConsoleText("sv_cheats 1\n")       // nav_generate is FCVAR_GAMEDLL|FCVAR_CHEAT
AddConsoleText("nav_generate\n")      // CommandNavGenerate -> BeginGeneration()
... pump RunFrame(); watch spew ...
AddConsoleText("quit\n")              // once the mesh is saved
```

`nav_generate` is *not* a quit-when-done path (only `BeginAnalysis(true)` sets
`m_bQuitWhenFinished`), so completion is detected from console output. The
generator prints, from `nav_generate.cpp` / `nav_mesh.cpp`:

```
Generation complete!  %0.1f seconds elapsed.
Navigation map '%s' saved.
```

A `tier0` spew hook (`SpewOutputFunc`, `navgen_spew.cpp`) watches for those
strings (and `Cannot save navigation map`), captures the saved path, and the
driver issues `quit` once the save is observed. A timeout guards against hangs.

The generated `.nav` is written by `CNavMesh::Save()`
(`game/server/nav_file.cpp`) to `<basedir>/<game>/maps/<map>.nav`.

## Linking model

The nav code is **not** linked into this tool — it ships inside the engine's
server module and is loaded at runtime. The tool links only the SDK
infrastructure libraries:

- `libtier0.so`, `libvstdlib.so` — prebuilt, shared (committed in the SDK).
- `CAppSystemGroup` / module loading — our own `appframework_min.cpp` (no
  prebuilt `appframework.a`); see "Self-contained appframework" below.
- `tier1` / `mathlib` — compiled from the SDK sources by the Makefile (the SDK
  drop ships them as sources, not prebuilt `.a`), and provide `Sys_LoadModule`,
  the `V_*` string utilities, containers, etc.

`appframework.a` and `tier1` have mutual references, so the link wraps them in
`-Wl,--start-group … --end-group`. The prebuilt libs use the pre-C++11
libstdc++ ABI, so everything is compiled with `-D_GLIBCXX_USE_CXX11_ABI=0`.

`engine.so` / `filesystem_stdio.so` / `libvstdlib.so` are resolved at runtime
from `<basedir>/bin/`; only `libtier0.so`/`libvstdlib.so` are needed at link
time (via `rpath`).

## Two build systems

- **Makefile** — direct host build. No container. Used for development and CI,
  and the path verified in this repo (compiles, links, runs up to the point of
  loading the Steam-distributed `engine.so`).
- **VPC + `buildallprojects`** — the canonical Source way. `navtools_create_mesh.vpc`
  (modeled on `utils/captioncompiler/captioncompiler.vpc`) is injected into the
  SDK solution by `scripts/integrate_sdk.sh` and built against the SteamRT
  *sniper* container. Required for anything distributed on Steam.

## Multicore batch generation

A single map's nav build is **single-threaded by design**: `nav_generate.cpp`
forces `host_thread_mode 0` ("need non-threaded server for light calcs") because
threading corrupts the lighting/analysis pass. So there is no safe way to make
one map's generation multicore.

The way to use many cores is **batch parallelism across maps**:
`scripts/navtools_batch.sh` runs up to `-j N` independent `navtools_create_mesh`
processes at once — one headless engine instance per map, each on its own UDP
port (`-port baseport+i`) — and aggregates results. This is ideal for
regenerating a dedicated server's whole map rotation. Each job is a separate
process, so it scales linearly with cores until disk/IO-bound.

## Self-contained appframework (no prebuilt appframework.a)

`CAppSystemGroup` (the module-loading / connect-init-shutdown lifecycle, and the
`ModInfo_t.m_pParentAppSystemGroup` the engine requires) is **reimplemented** in
`appframework_min.cpp`, matching `public/appframework/IAppSystemGroup.h`
(identical members + signatures). So neither build links a prebuilt
`appframework.a`. This was necessary because the SDKs ship no Linux appframework
**source**, and the classic SDK ships no Linux appframework library at all; it
also makes the 64-bit build dependency-free. The reimplementation drives the
documented order (Create → Connect → PreInit → Init → Main → Shutdown →
PostShutdown → Disconnect → Destroy) and its factory resolves interfaces by name
across registered systems (plus each system's `QueryInterface`, for modules that
expose several interfaces such as filesystem's `VFileSystem022` + `VBaseFileSystem011`).

## 32-bit dedicated server build

The default build is 64-bit (the vendored `source-sdk-2013` fork is 64-bit).
For a classic **32-bit** Linux dedicated server, `make ARCH=32` targets the
vendored classic SDK (`external/source-sdk-2013-classic`, the repo's
`singleplayer` branch — the last 32-bit layout, with 32-bit
`libtier0.so`/`libvstdlib.so`/`tier1.a`/`mathlib.a`). It is **self-contained**:

```bash
make ARCH=32          # needs g++-multilib  -> build/navtools_create_mesh32
```

Two classic-SDK Linux gaps (normally filled by Valve's `srcds` launcher binary)
are handled in-tree so no external inputs are needed:

1. **appframework** — provided by `appframework_min.cpp` (above).
2. **`g_pMemAlloc`** — the classic 32-bit `libtier0.so` doesn't export it (the
   64-bit one does), so `navgen_memalloc.cpp` supplies a malloc-backed
   `IMemAlloc` (compiled only for `ARCH=32`, gated by `NAVTOOLS_PROVIDE_MEMALLOC`).
   `navgen_compat.cpp` additionally restores the `__*_finite` math aliases the
   prebuilt archives expect (glibc ≥ 2.31 removed them).

**Verified here:** the 32-bit binary builds, links self-contained, loads the
classic 32-bit `libtier0`/`vstdlib` at runtime, runs our reimplemented
`CAppSystemGroup` lifecycle through the module-load attempt, and `--help`
works — i.e. the appframework reimpl and allocator shim are exercised without
crashing. **Not verifiable here:** actual generation (needs a 32-bit `srcds`
engine runtime) and the engine's acceptance of the reimplemented group during a
real map load. The engine (`engine.so`) is still `dlopen`'d at runtime from the
32-bit `srcds`, as in the 64-bit build.

## Tuning notes / caveats

- `settleFrames` (default 120) is a simple time gate after `map` before
  `nav_generate`. On slow-loading maps increase it (`-settle`) or the generate
  command may run before the world finishes spawning.
- The exact engine module name can differ between runtimes; override with
  `-engine`.
- `PreInit()` adds `BASE_PATH`/`EXECUTABLE_PATH` search paths and then lets the
  engine's `ModInit()` mount the rest from `gameinfo.txt`. If a particular
  runtime needs more paths pre-mounted, that is the place to add them.
