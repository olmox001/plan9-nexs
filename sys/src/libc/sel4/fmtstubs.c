/*
 * fmtstubs.c — Stub implementations for fmt library functions not available
 * in a freestanding seL4 environment.
 *
 * fmtlock: no-op on single-CPU cooperative PD (no concurrent fmt calls)
 * rerrstr: no-op stub (errfmt/%%r not needed in 9pserver or kernel libc)
 */

/* No includes needed — these are pure stubs */

void _fmtlock(void)  { }
void _fmtunlock(void){ }

/* Stub for rerrstr used by errfmt.c if linked */
void rerrstr(char *buf, unsigned int n) { if(n > 0) buf[0] = '\0'; }

/* fmt.c's verb dispatch table references these two; stub them out */
typedef struct Fmt Fmt;
int _efgfmt(Fmt *f) { (void)f; return -1; }
int errfmt(Fmt *f)  { (void)f; return -1; }
