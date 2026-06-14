//========================================================================//
// navtools_create_mesh - navgen_options.cpp
//========================================================================//
#include "navgen_options.h"

#include <cstdio>
#include <cstdlib>

#include "tier0/icommandline.h"

//-----------------------------------------------------------------------------
void PrintNavGenUsage()
{
	fprintf( stderr,
		"navtools_create_mesh - headless Source SDK 2013 navigation mesh generator\n"
		"\n"
		"Boots only the Source dedicated engine (no client, renderer or sound),\n"
		"loads a map's collision, runs nav_generate and writes <map>.nav.\n"
		"\n"
		"Usage:\n"
		"  navtools_create_mesh -game <moddir> -map <mapname> [options]\n"
		"\n"
		"Required:\n"
		"  -game <dir>      Mod/game directory containing gameinfo.txt\n"
		"  -map  <name>     Map name without .bsp (found under <game>/maps)\n"
		"\n"
		"Options:\n"
		"  -basedir <dir>   Engine base dir (contains bin/engine"
#ifdef _WIN32
		".dll"
#else
		".so"
#endif
		"). Defaults to\n"
		"                   $SOURCE_SDK_BASE, else the current directory.\n"
		"  -engine <name>   Engine module name (default: engine)\n"
		"  -incremental     Use nav_generate_incremental instead of nav_generate\n"
		"  -timeout <sec>   Abort if generation has not finished (default: 1800)\n"
		"  -settle <n>      Frames to wait after loading the map before generating\n"
		"                   (default: 120)\n"
		"  -v               Verbose: echo all engine console output\n"
		"  -h, --help       Show this help\n" );
}

//-----------------------------------------------------------------------------
bool ParseNavGenOptions( int argc, char ** /*argv*/, NavGenOptions &out )
{
	ICommandLine *cmd = CommandLine();

	if ( cmd->FindParm( "-h" ) || cmd->FindParm( "--help" ) || cmd->FindParm( "-help" ) )
		return false;

	out.gameDir      = cmd->ParmValue( "-game", (const char *)0 );
	out.mapName      = cmd->ParmValue( "-map", (const char *)0 );
	out.engineModule = cmd->ParmValue( "-engine", "engine" );
	out.incremental  = cmd->FindParm( "-incremental" ) != 0;
	out.verbose      = cmd->FindParm( "-v" ) != 0 || cmd->FindParm( "-verbose" ) != 0;
	out.timeoutSeconds = cmd->ParmValue( "-timeout", out.timeoutSeconds );
	out.settleFrames = cmd->ParmValue( "-settle", out.settleFrames );

	// -basedir, else $SOURCE_SDK_BASE, else "."
	out.baseDir = cmd->ParmValue( "-basedir", (const char *)0 );
	if ( !out.baseDir )
		out.baseDir = getenv( "SOURCE_SDK_BASE" );
	if ( !out.baseDir )
		out.baseDir = ".";

	if ( !out.gameDir )
	{
		fprintf( stderr, "error: -game <moddir> is required\n\n" );
		return false;
	}
	if ( !out.mapName )
	{
		fprintf( stderr, "error: -map <mapname> is required\n\n" );
		return false;
	}

	return true;
}
