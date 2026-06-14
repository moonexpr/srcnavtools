//========================================================================//
// navtools_create_mesh - navgen_spew.h
//
// A tier0 spew (console output) hook. The engine and game DLL talk to us only
// through Msg()/Warning()/DevMsg(), so we install a SpewOutputFunc to:
//   * forward output to our stdout/stderr (filtered unless verbose), and
//   * watch for the strings the nav generator prints so the driver loop knows
//     when generation has completed, saved, or failed.
//========================================================================//
#ifndef NAVGEN_SPEW_H
#define NAVGEN_SPEW_H

#ifdef _WIN32
#pragma once
#endif

// Install / remove the spew hook. Safe to call InstallSpewHook more than once
// (the engine resets the spew func during init, so we reinstall after ModInit).
void NavGen_InstallSpewHook( bool verbose );
void NavGen_RemoveSpewHook();

// Signals set by the hook while scanning engine/game output.
bool        NavGen_IsGenerationComplete();	// saw "Generation complete!"
bool        NavGen_DidSave();				// saw "Navigation map '...' saved."
bool        NavGen_SawSaveError();			// saw "Cannot save navigation map"
const char *NavGen_LastSavedFilename();		// best-effort parse of the saved path, or ""

#endif // NAVGEN_SPEW_H
