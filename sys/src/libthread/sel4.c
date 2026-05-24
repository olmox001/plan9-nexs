#include <u.h>
#include <libc.h>
#include <thread.h>
#include "threadimpl.h"

/*
 * launcher: Thread trampoline function.
 * Simply calls the thread entry function with its argument and calls threadexits.
 */
static void
launchersel4(int dummy, void (*f)(void *arg), void *arg)
{
	USED(dummy);
	(*f)(arg);
	threadexits(nil);
}

/*
 * _threadinitstack: Prepares the stack for a newly created thread.
 * It writes the argument and function pointers onto the stack, and sets up
 * the scheduling state t->sched (which is a jmp_buf) so that gotolabel
 * can jump to our launchersel4 with JMPBUFSP pointing to the new stack.
 */
void
_threadinitstack(Thread *t, void (*f)(void*), void *arg)
{
	uintptr *tos;

	/* TOS must be 16-byte aligned for AArch64 ABI */
	tos = (uintptr*)&t->stk[t->stksize & ~15];

	/* Lay out arguments and values on stack */
	*--tos = (uintptr)arg;
	*--tos = (uintptr)f;
	*--tos = 0;	/* dummy argument to launchersel4 */
	*--tos = 0;	/* slot to store return PC */

	t->sched[JMPBUFPC] = (uintptr)launchersel4;
	t->sched[JMPBUFSP] = (uintptr)tos;
}
