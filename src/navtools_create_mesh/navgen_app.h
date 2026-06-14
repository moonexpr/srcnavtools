//========================================================================//
// navtools_create_mesh - navgen_app.h
//
// CNavGenApp is the application system group that hosts a headless Source
// dedicated engine just long enough to generate a navigation mesh.
//
// It links against the Source SDK 2013 tier/appframework libraries and loads
// the engine, filesystem and cvar modules at runtime -- it never loads the
// client, renderer, sound or the full game, hence "without loading the whole
// game". The nav generation code itself lives in the engine-loaded server
// module; we only drive it through the dedicated server console API.
//========================================================================//
#ifndef NAVGEN_APP_H
#define NAVGEN_APP_H

#ifdef _WIN32
#pragma once
#endif

#include "appframework/IAppSystemGroup.h"
#include "navgen_options.h"

class IFileSystem;
class ICvar;
class IDedicatedServerAPI;

//-----------------------------------------------------------------------------
// The app group. CAppSystemGroup::Run() drives the standard lifecycle:
//   Create -> Connect -> PreInit -> Init -> Main -> Shutdown -> ... -> Destroy
//-----------------------------------------------------------------------------
class CNavGenApp : public CAppSystemGroup
{
	typedef CAppSystemGroup BaseClass;

public:
	explicit CNavGenApp( const NavGenOptions &options );

	// CAppSystemGroup
	virtual bool	Create();			// load filesystem / cvar / engine modules
	virtual bool	PreInit();			// set up filesystem search paths
	virtual int		Main();				// ModInit + drive generation, return process exit code
	virtual void	PostShutdown();
	virtual void	Destroy();

private:
	// Build "<basedir>/bin/<module><ext>" into pOut.
	void			BuildBinPath( char *pOut, int outLen, const char *pModule ) const;

	// The generation driver: feed console commands and watch spew until done.
	int				RunGeneration();

	const NavGenOptions	&m_options;

	IFileSystem			*m_pFileSystem;
	ICvar				*m_pCvar;
	IDedicatedServerAPI	*m_pDedicatedAPI;
};

#endif // NAVGEN_APP_H
