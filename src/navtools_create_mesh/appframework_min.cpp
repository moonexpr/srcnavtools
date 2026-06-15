//========================================================================//
// navtools_create_mesh - appframework_min.cpp
//
// A self-contained reimplementation of CAppSystemGroup, so the tool does NOT
// depend on a prebuilt appframework.a. This matters because:
//   * the classic 32-bit SDK ships no Linux appframework at all, and
//   * it lets the 64-bit build drop the prebuilt-lib dependency too.
//
// It implements exactly the CAppSystemGroup declared in
// public/appframework/IAppSystemGroup.h (same data members + method
// signatures), driving the documented lifecycle:
//   Create -> ConnectSystems -> PreInit -> InitSystems -> Main
//          -> ShutdownSystems -> PostShutdown -> DisconnectSystems -> Destroy
// The members (CUtlVector/CUtlDict) and behaviour mirror Valve's appframework
// so the engine, which receives this object as ModInfo_t.m_pParentAppSystemGroup,
// sees an equivalent group.
//========================================================================//
#include "appframework/IAppSystemGroup.h"
#include "tier1/interface.h"
#include "tier0/dbg.h"

#include <cstdlib>
#include <cstring>
#include <strings.h>   // strcasecmp

//-----------------------------------------------------------------------------
// The factory handed to IAppSystem::Connect and to the engine: resolves an
// interface by name across the registered systems of the topmost group.
//-----------------------------------------------------------------------------
static CAppSystemGroup *s_pTopGroup = NULL;

void *AppSystemCreateInterfaceFn( const char *pName, int *pReturnCode )
{
	void *p = s_pTopGroup ? s_pTopGroup->FindSystem( pName ) : NULL;
	if ( pReturnCode )
		*pReturnCode = p ? IFACE_OK : IFACE_FAILED;
	return p;
}

//-----------------------------------------------------------------------------
CAppSystemGroup::CAppSystemGroup( CAppSystemGroup *pParentAppSystem )
	: m_pParentAppSystem( pParentAppSystem ), m_nErrorStage( NONE )
{
}

CSysModule *CAppSystemGroup::LoadModuleDLL( const char *pDLLName )
{
	return Sys_LoadModule( pDLLName );
}

AppModule_t CAppSystemGroup::LoadModule( const char *pDLLName )
{
	// Already loaded?
	for ( int i = 0; i < m_Modules.Count(); ++i )
		if ( m_Modules[i].m_pModuleName && !strcasecmp( m_Modules[i].m_pModuleName, pDLLName ) )
			return i;

	CSysModule *pModule = LoadModuleDLL( pDLLName );
	if ( !pModule )
		return APP_MODULE_INVALID;

	CreateInterfaceFn fn = Sys_GetFactory( pModule );
	if ( !fn )
	{
		Sys_UnloadModule( pModule );
		return APP_MODULE_INVALID;
	}

	Module_t module;
	module.m_pModule = pModule;
	module.m_Factory = fn;
	module.m_pModuleName = strdup( pDLLName );
	return m_Modules.AddToTail( module );
}

AppModule_t CAppSystemGroup::LoadModule( CreateInterfaceFn factory )
{
	if ( !factory )
		return APP_MODULE_INVALID;
	Module_t module;
	module.m_pModule = NULL;
	module.m_Factory = factory;
	module.m_pModuleName = NULL;
	return m_Modules.AddToTail( module );
}

IAppSystem *CAppSystemGroup::AddSystem( AppModule_t module, const char *pInterfaceName )
{
	if ( module == APP_MODULE_INVALID )
		return NULL;

	void *p = m_Modules[module].m_Factory( pInterfaceName, NULL );
	if ( !p )
	{
		Warning( "AppFramework: unable to find interface '%s'\n", pInterfaceName );
		return NULL;
	}

	IAppSystem *pSystem = static_cast< IAppSystem * >( p );
	int idx = m_Systems.AddToTail( pSystem );
	m_SystemDict.Insert( pInterfaceName, idx );
	return pSystem;
}

void CAppSystemGroup::AddSystem( IAppSystem *pSystem, const char *pInterfaceName )
{
	if ( !pSystem )
		return;
	int idx = m_Systems.AddToTail( pSystem );
	m_SystemDict.Insert( pInterfaceName, idx );
}

void *CAppSystemGroup::FindSystem( const char *pInterfaceName )
{
	unsigned short i = m_SystemDict.Find( pInterfaceName );
	if ( i != m_SystemDict.InvalidIndex() )
		return m_Systems[ m_SystemDict[i] ];

	// A module may expose several interfaces (e.g. filesystem exposes both
	// VFileSystem022 and VBaseFileSystem011): ask each system, then each module
	// factory, then the parent group.
	for ( int s = 0; s < m_Systems.Count(); ++s )
	{
		void *p = m_Systems[s]->QueryInterface( pInterfaceName );
		if ( p )
			return p;
	}
	for ( int m = 0; m < m_Modules.Count(); ++m )
	{
		if ( m_Modules[m].m_Factory )
		{
			void *p = m_Modules[m].m_Factory( pInterfaceName, NULL );
			if ( p )
				return p;
		}
	}
	if ( m_pParentAppSystem )
		return m_pParentAppSystem->FindSystem( pInterfaceName );
	return NULL;
}

CreateInterfaceFn CAppSystemGroup::GetFactory()
{
	return AppSystemCreateInterfaceFn;
}

CAppSystemGroup *CAppSystemGroup::GetParent()
{
	return m_pParentAppSystem;
}

//-----------------------------------------------------------------------------
// Lifecycle helpers
//-----------------------------------------------------------------------------
bool CAppSystemGroup::ConnectSystems()
{
	CreateInterfaceFn factory = GetFactory();
	for ( int i = 0; i < m_Systems.Count(); ++i )
		if ( !m_Systems[i]->Connect( factory ) )
			return false;
	return true;
}

void CAppSystemGroup::DisconnectSystems()
{
	for ( int i = m_Systems.Count() - 1; i >= 0; --i )
		m_Systems[i]->Disconnect();
}

InitReturnVal_t CAppSystemGroup::InitSystems()
{
	for ( int i = 0; i < m_Systems.Count(); ++i )
		if ( m_Systems[i]->Init() != INIT_OK )
			return INIT_FAILED;
	return INIT_OK;
}

void CAppSystemGroup::ShutdownSystems()
{
	for ( int i = m_Systems.Count() - 1; i >= 0; --i )
		m_Systems[i]->Shutdown();
}

void CAppSystemGroup::UnloadAllModules()
{
	for ( int i = m_Modules.Count() - 1; i >= 0; --i )
	{
		if ( m_Modules[i].m_pModule )
			Sys_UnloadModule( m_Modules[i].m_pModule );
		if ( m_Modules[i].m_pModuleName )
			free( m_Modules[i].m_pModuleName );
	}
	m_Modules.RemoveAll();
}

void CAppSystemGroup::RemoveAllSystems()
{
	m_Systems.RemoveAll();
	m_SystemDict.RemoveAll();
}

//-----------------------------------------------------------------------------
int CAppSystemGroup::Run()
{
	s_pTopGroup = this;

	if ( !Create() )            { m_nErrorStage = CREATION;         return -1; }
	if ( !ConnectSystems() )    { m_nErrorStage = CONNECTION;       DisconnectSystems(); return -1; }
	if ( !PreInit() )           { m_nErrorStage = PREINITIALIZATION; return -1; }
	if ( InitSystems() != INIT_OK ) { m_nErrorStage = INITIALIZATION; ShutdownSystems(); return -1; }

	int nRetVal = Main();

	ShutdownSystems();
	PostShutdown();
	DisconnectSystems();
	Destroy();
	UnloadAllModules();
	return nRetVal;
}

int CAppSystemGroup::Startup()
{
	s_pTopGroup = this;
	if ( !Create() )                { m_nErrorStage = CREATION;          return -1; }
	if ( !ConnectSystems() )        { m_nErrorStage = CONNECTION;        return -1; }
	if ( !PreInit() )               { m_nErrorStage = PREINITIALIZATION; return -1; }
	if ( InitSystems() != INIT_OK ) { m_nErrorStage = INITIALIZATION;    return -1; }
	return 0;
}

void CAppSystemGroup::Shutdown()
{
	ShutdownSystems();
	PostShutdown();
	DisconnectSystems();
	Destroy();
	UnloadAllModules();
}

CAppSystemGroup::AppSystemGroupStage_t CAppSystemGroup::GetErrorStage() const
{
	return m_nErrorStage;
}
