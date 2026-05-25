#include <u.h>
#include <libc.h>
#include "../../9/sel4/include/tos.h"

/* Global Top-Of-Stack pointer */
Tos *_tos = nil;
char *argv0;

extern void main(int argc, char **argv);

/*
 * Entry point for Plan 9 user processes on seL4.
 * When exec9p jumps to the ELF entry point, it passes a pointer
 * to the Tos structure in R0 (x0), which GCC exposes as the first argument.
 */
void
_start(Tos *tos)
{
	_tos = tos;
	if(tos && tos->argc > 0)
		argv0 = tos->argv[0];
	else
		argv0 = "plan9_proc";

	/* Call the standard Plan 9 main function */
	main(tos ? tos->argc : 0, tos ? tos->argv : nil);

	/* Exit gracefully if main returns */
	exits(nil);
}
