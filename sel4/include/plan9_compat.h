#ifndef _PLAN9_COMPAT_H_
#define _PLAN9_COMPAT_H_

#include <u.h>
#include <ureg.h>

/* Ignore Plan 9 compiler-specific pragmas under GCC/Clang */
#define incomplete
#define varargck

/* Plan 9 custom unused variable/assignment macros */
#define USED(...)	((void)(__VA_ARGS__))
#define SET(x)		((void)(x))

/* 
 * Map Plan 9's UTF-8 non-ASCII function name µs (U+00B5) to ASCII 'us'
 * using standard GCC/Clang universal character name (UCN) notation.
 */
#define \u00B5s		us

/* 
 * Patch for print.c _efgfmt(Fmt*) declaration, providing a dummy 
 * parameter name to make it standard C.
 */
#ifdef COMPILING_KERNEL
#define _efgfmt(x)	_efgfmt(x _dummy)

/*
 * Patches for dev.c function definitions with omitted parameter names.
 * These macros expand the typenames in the function definitions to include dummy parameter names.
 */
#define devcreate(t1, t2, t3, t4) devcreate(t1 c_dummy, t2 s_dummy, t3 m_dummy, t4 a_dummy)
#define devremove(t1)             devremove(t1 c_dummy)
#define devwstat(t1, t2, t3)      devwstat(t1 c_dummy, t2 b_dummy, t3 n_dummy)
#define devpower(t1)              devpower(t1 p_dummy)
#define devconfig(t1, t2, t3)     devconfig(t1 i_dummy, t2 s_dummy, t3 d_dummy)

/* Declaration for _barrier shim */
extern uintptr _barrier(uintptr);
#endif

#endif /* _PLAN9_COMPAT_H_ */
