//========================================================================//
// navtools_create_mesh - navgen_spew.cpp
//========================================================================//
#include "navgen_spew.h"

#include <cstdio>
#include <cstring>

#include "tier0/dbg.h"

//-----------------------------------------------------------------------------
// State updated by the spew callback.
//-----------------------------------------------------------------------------
static bool				s_verbose = false;
static SpewOutputFunc_t	s_pPrevSpew = 0;

static volatile bool	s_bComplete = false;
static volatile bool	s_bSaved = false;
static volatile bool	s_bSaveError = false;
static char				s_savedFilename[512] = { 0 };

//-----------------------------------------------------------------------------
// Pull "<map>.nav" out of:  Navigation map 'maps/foo.nav' saved.
//-----------------------------------------------------------------------------
static void CaptureSavedFilename( const char *pMsg )
{
	const char *open = strchr( pMsg, '\'' );
	if ( !open )
		return;
	const char *close = strchr( open + 1, '\'' );
	if ( !close )
		return;

	size_t len = (size_t)( close - ( open + 1 ) );
	if ( len >= sizeof( s_savedFilename ) )
		len = sizeof( s_savedFilename ) - 1;
	memcpy( s_savedFilename, open + 1, len );
	s_savedFilename[len] = '\0';
}

//-----------------------------------------------------------------------------
// The hook itself. Runs on the engine's main thread inside Msg()/Warning().
//-----------------------------------------------------------------------------
static SpewRetval_t NavGenSpewOutput( SpewType_t spewType, const tchar *pMsg )
{
	if ( pMsg )
	{
		// Watch for the nav generator's progress strings (see nav_generate.cpp
		// / nav_mesh.cpp in the SDK).
		if ( strstr( pMsg, "Generation complete" ) )
			s_bComplete = true;

		if ( strstr( pMsg, "Navigation map" ) && strstr( pMsg, "saved" ) )
		{
			s_bSaved = true;
			CaptureSavedFilename( pMsg );
		}

		if ( strstr( pMsg, "Cannot save navigation map" ) )
			s_bSaveError = true;

		// Forward to our console. In quiet mode only surface warnings/errors and
		// the nav generator's own messages so the run stays readable.
		bool important =
			spewType == SPEW_WARNING ||
			spewType == SPEW_ASSERT  ||
			spewType == SPEW_ERROR   ||
			s_bComplete || s_bSaved || s_bSaveError;

		if ( s_verbose || important )
		{
			FILE *out = ( spewType == SPEW_WARNING || spewType == SPEW_ASSERT ||
						  spewType == SPEW_ERROR ) ? stderr : stdout;
			fputs( pMsg, out );
			fflush( out );
		}
	}

	// Never abort: let the engine continue its own error handling.
	if ( spewType == SPEW_ASSERT )
		return SPEW_CONTINUE;

	return SPEW_CONTINUE;
}

//-----------------------------------------------------------------------------
void NavGen_InstallSpewHook( bool verbose )
{
	s_verbose = verbose;
	SpewOutputFunc_t cur = GetSpewOutputFunc();
	if ( cur != NavGenSpewOutput )
		s_pPrevSpew = cur;
	SpewOutputFunc( NavGenSpewOutput );
}

void NavGen_RemoveSpewHook()
{
	if ( GetSpewOutputFunc() == NavGenSpewOutput )
		SpewOutputFunc( s_pPrevSpew );
}

bool        NavGen_IsGenerationComplete() { return s_bComplete; }
bool        NavGen_DidSave()              { return s_bSaved; }
bool        NavGen_SawSaveError()         { return s_bSaveError; }
const char *NavGen_LastSavedFilename()    { return s_savedFilename; }
