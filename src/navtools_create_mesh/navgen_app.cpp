//========================================================================//
// navtools_create_mesh - navgen_app.cpp
//========================================================================//
#include "navgen_app.h"
#include "navgen_spew.h"

#include <cstdio>
#include <cstring>

#include "tier0/dbg.h"
#include "tier0/platform.h"		// Plat_FloatTime
#include "tier1/interface.h"
#include "filesystem.h"			// IFileSystem, FILESYSTEM_INTERFACE_VERSION
#include "icvar.h"				// ICvar, CVAR_INTERFACE_VERSION
#include "engine_hlds_api.h"	// IDedicatedServerAPI, ModInfo_t, VENGINE_HLDS_API_VERSION

#ifdef _WIN32
static const char *kDllExt = ".dll";
#else
static const char *kDllExt = ".so";
#endif

//-----------------------------------------------------------------------------
CNavGenApp::CNavGenApp( const NavGenOptions &options )
	: m_options( options )
	, m_pFileSystem( 0 )
	, m_pCvar( 0 )
	, m_pDedicatedAPI( 0 )
{
}

//-----------------------------------------------------------------------------
void CNavGenApp::BuildBinPath( char *pOut, int outLen, const char *pModule ) const
{
	snprintf( pOut, (size_t)outLen, "%s/bin/%s%s", m_options.baseDir, pModule, kDllExt );
}

//-----------------------------------------------------------------------------
// Load only the modules we need: the filesystem, the cvar system (vstdlib) and
// the engine's dedicated-server API. The engine pulls in the server module --
// which contains the nav generation code -- on its own when we load a map. We
// never touch the client, renderer, sound or materialsystem.
//-----------------------------------------------------------------------------
bool CNavGenApp::Create()
{
	char path[1024];

	// On POSIX the cvar system ships as libvstdlib.so; on Windows it is vstdlib.dll.
#ifdef _WIN32
	const char *cvarModule = "vstdlib";
#else
	const char *cvarModule = "libvstdlib";
#endif

	// Filesystem first -- everything else is located through it.
	BuildBinPath( path, sizeof( path ), "filesystem_stdio" );
	AppModule_t fsMod = LoadModule( path );
	if ( fsMod == APP_MODULE_INVALID )
	{
		Warning( "navtools_create_mesh: failed to load %s\n", path );
		return false;
	}
	m_pFileSystem = (IFileSystem *)AddSystem( fsMod, FILESYSTEM_INTERFACE_VERSION );

	// Cvar system (vstdlib).
	BuildBinPath( path, sizeof( path ), cvarModule );
	AppModule_t cvarMod = LoadModule( path );
	if ( cvarMod == APP_MODULE_INVALID )
	{
		Warning( "navtools_create_mesh: failed to load %s\n", path );
		return false;
	}
	m_pCvar = (ICvar *)AddSystem( cvarMod, CVAR_INTERFACE_VERSION );

	// Engine (headless dedicated server API).
	BuildBinPath( path, sizeof( path ), m_options.engineModule );
	AppModule_t engMod = LoadModule( path );
	if ( engMod == APP_MODULE_INVALID )
	{
		Warning( "navtools_create_mesh: failed to load %s\n", path );
		return false;
	}
	m_pDedicatedAPI = (IDedicatedServerAPI *)AddSystem( engMod, VENGINE_HLDS_API_VERSION );

	if ( !m_pFileSystem || !m_pCvar || !m_pDedicatedAPI )
	{
		Warning( "navtools_create_mesh: a required engine interface was not found.\n"
				 "  filesystem=%p cvar=%p engine=%p\n",
				 (void *)m_pFileSystem, (void *)m_pCvar, (void *)m_pDedicatedAPI );
		return false;
	}

	return true;
}

//-----------------------------------------------------------------------------
// Connected-but-not-yet-initialized stage. Mount the engine base directory so
// the engine can find bin/ and the mod's gameinfo.txt during ModInit.
//-----------------------------------------------------------------------------
bool CNavGenApp::PreInit()
{
	if ( !m_pFileSystem )
		return false;

	m_pFileSystem->AddSearchPath( m_options.baseDir, "BASE_PATH" );
	m_pFileSystem->AddSearchPath( m_options.baseDir, "EXECUTABLE_PATH" );
	return true;
}

//-----------------------------------------------------------------------------
int CNavGenApp::Main()
{
	// The engine resets the spew function during its own init, so reinstall ours
	// now that all systems are connected and initialized.
	NavGen_InstallSpewHook( m_options.verbose );

	ModInfo_t info;
	memset( &info, 0, sizeof( info ) );
	info.m_pInstance				= 0;
	info.m_pBaseDirectory			= m_options.baseDir;
	info.m_pInitialMod				= m_options.gameDir;
	info.m_pInitialGame				= m_options.gameDir;
	info.m_pParentAppSystemGroup	= this;
	info.m_bTextMode				= true;	// no client window

	if ( !m_pDedicatedAPI->ModInit( info ) )
	{
		Warning( "navtools_create_mesh: engine ModInit() failed "
				 "(basedir='%s', game='%s')\n", m_options.baseDir, m_options.gameDir );
		return 1;
	}

	int rc = RunGeneration();

	m_pDedicatedAPI->ModShutdown();
	return rc;
}

//-----------------------------------------------------------------------------
// Drive the dedicated console: load the map, enable cheats, run nav_generate,
// and wait (watching spew) until the .nav file is saved -- then quit.
//-----------------------------------------------------------------------------
int CNavGenApp::RunGeneration()
{
	enum Phase
	{
		WAIT_MAP,	// map command issued; let the world spawn
		WAIT_DONE,	// nav_generate issued; wait for save / completion
	};
	Phase phase = WAIT_MAP;

	char cmd[256];
	snprintf( cmd, sizeof( cmd ), "map %s\n", m_options.mapName );
	m_pDedicatedAPI->AddConsoleText( cmd );
	Msg( "navtools_create_mesh: loading map '%s' ...\n", m_options.mapName );

	const double startTime = Plat_FloatTime();
	int framesSinceMap = 0;
	bool quitIssued = false;

	while ( m_pDedicatedAPI->RunFrame() )
	{
		if ( Plat_FloatTime() - startTime > m_options.timeoutSeconds )
		{
			Warning( "navtools_create_mesh: timed out after %.0f seconds\n",
					 m_options.timeoutSeconds );
			return 1;
		}

		switch ( phase )
		{
		case WAIT_MAP:
			if ( ++framesSinceMap >= m_options.settleFrames )
			{
				// nav_generate is FCVAR_CHEAT, so cheats must be enabled.
				m_pDedicatedAPI->AddConsoleText( (char *)"sv_cheats 1\n" );

				const char *gen = m_options.incremental
					? "nav_generate_incremental\n"
					: "nav_generate\n";
				m_pDedicatedAPI->AddConsoleText( (char *)gen );

				Msg( "navtools_create_mesh: running %s",
					 m_options.incremental ? "nav_generate_incremental\n" : "nav_generate\n" );
				phase = WAIT_DONE;
			}
			break;

		case WAIT_DONE:
			if ( NavGen_SawSaveError() && !quitIssued )
			{
				Warning( "navtools_create_mesh: the engine reported a save failure\n" );
				m_pDedicatedAPI->AddConsoleText( (char *)"quit\n" );
				quitIssued = true;
			}
			else if ( NavGen_DidSave() && !quitIssued )
			{
				Msg( "navtools_create_mesh: saved '%s'\n", NavGen_LastSavedFilename() );
				m_pDedicatedAPI->AddConsoleText( (char *)"quit\n" );
				quitIssued = true;
			}
			break;
		}
	}

	// Engine has shut down. Decide the exit code from what we observed.
	if ( NavGen_SawSaveError() )
		return 1;
	if ( NavGen_DidSave() || NavGen_IsGenerationComplete() )
		return 0;

	Warning( "navtools_create_mesh: engine exited before generation completed\n" );
	return 1;
}

//-----------------------------------------------------------------------------
void CNavGenApp::PostShutdown()
{
}

void CNavGenApp::Destroy()
{
}
