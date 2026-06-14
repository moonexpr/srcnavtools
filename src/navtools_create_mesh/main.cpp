//========================================================================//
// navtools_create_mesh
//
// A headless command-line tool that links against the Source SDK 2013
// tier/appframework libraries and drives the Source dedicated engine just far
// enough to generate a navigation mesh (.nav) for a single map -- without
// loading the client, renderer, sound or the rest of the game.
//
// See the engine console command this ultimately invokes:
//   https://github.com/ValveSoftware/source-sdk-2013/blob/master/src/game/server/nav_generate.cpp
//
// Usage:
//   navtools_create_mesh -game <moddir> -map <mapname> [options]
//========================================================================//
#include <cstdio>

#include "tier0/dbg.h"
#include "tier0/icommandline.h"

#include "navgen_options.h"
#include "navgen_app.h"
#include "navgen_spew.h"

int main( int argc, char **argv )
{
	// tier0's command line is what the engine reads internally, so populate it
	// from our argv before doing anything else.
	CommandLine()->CreateCmdLine( argc, argv );

	NavGenOptions options;
	if ( !ParseNavGenOptions( argc, argv, options ) )
	{
		PrintNavGenUsage();
		return 1;
	}

	// Install the spew hook early so we capture output from the very first
	// engine messages; CNavGenApp reinstalls it after the engine inits.
	NavGen_InstallSpewHook( options.verbose );

	int rc;
	{
		CNavGenApp app( options );
		rc = app.Run();
	}

	NavGen_RemoveSpewHook();

	if ( rc == 0 )
		fprintf( stderr, "navtools_create_mesh: done.\n" );
	else
		fprintf( stderr, "navtools_create_mesh: failed (exit %d).\n", rc );

	return rc;
}
