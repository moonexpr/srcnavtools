//========================================================================//
// navtools_create_mesh - navgen_memalloc.cpp
//
// A minimal malloc-backed IMemAlloc that provides the global g_pMemAlloc.
//
// Needed ONLY for the 32-bit (classic SDK) build: the classic Linux libtier0.so
// does not export g_pMemAlloc (the executable is expected to supply it, the way
// Valve's srcds launcher does). The 64-bit fork's libtier0.so exports it, so
// this file must NOT be compiled there (it is gated by NAVTOOLS_PROVIDE_MEMALLOC,
// which the Makefile defines only for ARCH=32).
//
// Every IMemAlloc virtual is overridden so the vtable matches the prebuilt
// tier1/tier0 exactly (the compiler enforces completeness; method order is
// fixed by the header). Only Alloc/Realloc/Free/GetSize do real work; the
// debug/CRT entry points are no-ops, which is sufficient for our launcher's
// small allocations.
//========================================================================//
#if defined( NAVTOOLS_PROVIDE_MEMALLOC )

#include "tier0/memalloc.h"

#include <cstdlib>
#include <malloc.h>   // malloc_usable_size

class CNavToolsMemAlloc : public IMemAlloc
{
public:
	// Release versions
	virtual void *Alloc( size_t nSize ) { return malloc( nSize ); }
	virtual void *Realloc( void *pMem, size_t nSize ) { return realloc( pMem, nSize ); }
	virtual void Free( void *pMem ) { free( pMem ); }
	virtual void *Expand_NoLongerSupported( void *, size_t ) { return NULL; }

	// Debug versions
	virtual void *Alloc( size_t nSize, const char *, int ) { return malloc( nSize ); }
	virtual void *Realloc( void *pMem, size_t nSize, const char *, int ) { return realloc( pMem, nSize ); }
	virtual void Free( void *pMem, const char *, int ) { free( pMem ); }
	virtual void *Expand_NoLongerSupported( void *, size_t, const char *, int ) { return NULL; }

	virtual size_t GetSize( void *pMem ) { return pMem ? malloc_usable_size( pMem ) : 0; }

	virtual void PushAllocDbgInfo( const char *, int ) {}
	virtual void PopAllocDbgInfo() {}

	virtual long CrtSetBreakAlloc( long ) { return 0; }
	virtual int CrtSetReportMode( int, int ) { return 0; }
	virtual int CrtIsValidHeapPointer( const void * ) { return 1; }
	virtual int CrtIsValidPointer( const void *, unsigned int, int ) { return 1; }
	virtual int CrtCheckMemory( void ) { return 1; }
	virtual int CrtSetDbgFlag( int ) { return 0; }
	virtual void CrtMemCheckpoint( _CrtMemState * ) {}

	virtual void DumpStats() {}
	virtual void DumpStatsFileBase( char const * ) {}

	virtual void *CrtSetReportFile( int, void * ) { return NULL; }
	virtual void *CrtSetReportHook( void * ) { return NULL; }
	virtual int CrtDbgReport( int, const char *, int, const char *, const char * ) { return 0; }

	virtual int heapchk() { return 1; } // _HEAPOK-ish; value unused by our path

	virtual bool IsDebugHeap() { return false; }

	virtual void GetActualDbgInfo( const char *&pFileName, int &nLine ) { pFileName = "<unknown>"; nLine = 0; }
	virtual void RegisterAllocation( const char *, int, int, int, unsigned ) {}
	virtual void RegisterDeallocation( const char *, int, int, int, unsigned ) {}

	virtual int GetVersion() { return 1; }

	virtual void CompactHeap() {}

	virtual MemAllocFailHandler_t SetAllocFailHandler( MemAllocFailHandler_t pfn ) { return pfn; }

	virtual void DumpBlockStats( void * ) {}

	virtual size_t MemoryAllocFailed() { return 0; }

	virtual uint32 GetDebugInfoSize() { return 0; }
	virtual void SaveDebugInfo( void * ) {}
	virtual void RestoreDebugInfo( const void * ) {}
	virtual void InitDebugInfo( void *, const char *, int ) {}

	virtual void GlobalMemoryStatus( size_t *pUsedMemory, size_t *pFreeMemory )
	{
		if ( pUsedMemory ) *pUsedMemory = 0;
		if ( pFreeMemory ) *pFreeMemory = 0;
	}
};

static CNavToolsMemAlloc s_NavToolsMemAlloc;
IMemAlloc *g_pMemAlloc = &s_NavToolsMemAlloc;

#endif // NAVTOOLS_PROVIDE_MEMALLOC
