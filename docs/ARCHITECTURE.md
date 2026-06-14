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
- `appframework.a` — prebuilt, static (`CAppSystemGroup`, module loading).
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

## Tuning notes / caveats

- `settleFrames` (default 120) is a simple time gate after `map` before
  `nav_generate`. On slow-loading maps increase it (`-settle`) or the generate
  command may run before the world finishes spawning.
- The exact engine module name can differ between runtimes; override with
  `-engine`.
- `PreInit()` adds `BASE_PATH`/`EXECUTABLE_PATH` search paths and then lets the
  engine's `ModInit()` mount the rest from `gameinfo.txt`. If a particular
  runtime needs more paths pre-mounted, that is the place to add them.
