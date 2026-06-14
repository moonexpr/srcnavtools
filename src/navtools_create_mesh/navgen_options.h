//========================================================================//
// navtools_create_mesh - headless Source SDK 2013 navigation mesh generator
//
// navgen_options.h
//
// Command-line options for the tool, parsed once at startup and then passed
// down into the app system group that drives generation.
//========================================================================//
#ifndef NAVGEN_OPTIONS_H
#define NAVGEN_OPTIONS_H

#ifdef _WIN32
#pragma once
#endif

//-----------------------------------------------------------------------------
// Parsed command-line options. Pointers reference argv / the engine command
// line buffer, which lives for the entire run, so we do not own them.
//-----------------------------------------------------------------------------
struct NavGenOptions
{
	const char	*gameDir;			// -game <dir>     : mod dir containing gameinfo.txt (required)
	const char	*mapName;			// -map  <name>    : map name without .bsp        (required)
	const char	*baseDir;			// -basedir <dir>  : engine base dir (contains bin/engine.so)
	const char	*engineModule;		// -engine <name>  : engine module name (default "engine")
	bool		incremental;		// -incremental    : use nav_generate_incremental
	bool		verbose;			// -v              : echo all engine console spew
	float		timeoutSeconds;		// -timeout <sec>  : abort if not finished in time
	int			settleFrames;		// -settle <n>     : frames to wait after "map" before generating

	NavGenOptions()
		: gameDir( 0 )
		, mapName( 0 )
		, baseDir( 0 )
		, engineModule( "engine" )
		, incremental( false )
		, verbose( false )
		, timeoutSeconds( 1800.0f )
		, settleFrames( 120 )
	{
	}
};

// Parse argv into out. Returns false if required options are missing or -h/--help
// was requested (caller should then print usage). Uses the tier0 CommandLine().
bool ParseNavGenOptions( int argc, char **argv, NavGenOptions &out );

// Print usage to stderr.
void PrintNavGenUsage();

#endif // NAVGEN_OPTIONS_H
