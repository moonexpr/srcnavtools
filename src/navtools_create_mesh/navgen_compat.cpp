//========================================================================//
// navtools_create_mesh - navgen_compat.cpp
//
// glibc compatibility shims for the 32-bit (classic SDK) build only.
//
// The classic 32-bit tier1.a / mathlib.a were compiled against an old glibc
// that exported the __*_finite math aliases (emitted under -ffast-math /
// -ffinite-math-only). glibc >= 2.31 removed those public aliases, so linking
// the prebuilt archives against a modern toolchain fails with
// "undefined reference to `__pow_finite'" etc. Provide thin forwarders.
//
// Gated by NAVTOOLS_PROVIDE_MEMALLOC (defined only for ARCH=32) so it is never
// compiled into the 64-bit build (whose tier1/mathlib we build from source).
//========================================================================//
#if defined( NAVTOOLS_PROVIDE_MEMALLOC )

#include <math.h>

// Compiled without -ffinite-math-only, so the standard calls below resolve to
// the real libm functions (no self-recursion).
extern "C" {

double __pow_finite( double x, double y )      { return pow( x, y ); }
double __exp_finite( double x )                { return exp( x ); }
double __exp2_finite( double x )               { return exp2( x ); }
double __log_finite( double x )                { return log( x ); }
double __log2_finite( double x )               { return log2( x ); }
double __log10_finite( double x )              { return log10( x ); }
double __acos_finite( double x )               { return acos( x ); }
double __asin_finite( double x )               { return asin( x ); }
double __atan2_finite( double y, double x )    { return atan2( y, x ); }
double __cosh_finite( double x )               { return cosh( x ); }
double __sinh_finite( double x )               { return sinh( x ); }

float __powf_finite( float x, float y )        { return powf( x, y ); }
float __expf_finite( float x )                 { return expf( x ); }
float __exp2f_finite( float x )                { return exp2f( x ); }
float __logf_finite( float x )                 { return logf( x ); }
float __log2f_finite( float x )                { return log2f( x ); }
float __log10f_finite( float x )               { return log10f( x ); }
float __acosf_finite( float x )                { return acosf( x ); }
float __asinf_finite( float x )                { return asinf( x ); }
float __atan2f_finite( float y, float x )      { return atan2f( y, x ); }
float __coshf_finite( float x )                { return coshf( x ); }
float __sinhf_finite( float x )                { return sinhf( x ); }

} // extern "C"

#endif // NAVTOOLS_PROVIDE_MEMALLOC
