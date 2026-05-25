/*
 * tos.h — Top-Of-Stack structure for Plan 9 processes on seL4.
 *
 * When plan9_root creates a new user process via exec9p(), it places
 * a Tos structure at the top of the new process's stack and passes
 * a pointer to it in R0 (AArch64 ABI first argument register).
 *
 * The user process entry point (_sel4_start in libc/sel4/main9.c)
 * saves the Tos pointer in _tos, then calls main(argc, argv).
 *
 * The syscall gate (tos->gate) is a function pointer into plan9_root's
 * syscall dispatcher. Since all processes run in the same address space
 * (single-AS seL4 Microkit PD), this is a direct call — no SVC/trap
 * needed. The Plan 9 libc for the sel4 target calls through this pointer.
 *
 * Syscall convention:
 *   gate(sysno, a0, a1, a2, a3, a4, a5)
 *   sysno = Plan 9 syscall number (from 9syscall/sys.h)
 *   a0..a5 = arguments (cast to long)
 *   return value: long (-1 on error, errstr set in up->errstr)
 */

#ifndef _TOS_H_
#define _TOS_H_

typedef struct Tos Tos;

struct Tos {
	/* Syscall gate — MUST be first field.
	 * plan9_root writes the dispatcher address here.
	 * User processes call through it for all syscalls. */
	long	(*gate)(long sysno, long a0, long a1, long a2,
	                long a3, long a4, long a5);

	/* Process identity */
	int	pid;
	int	ppid;

	/* Argument vector (set by exec9p before jumping to entry) */
	int	argc;
	char	**argv;
	char	**envp;

	/* Timing counters (Plan 9 standard) */
	unsigned long long cyclefreq;   /* cycles per second */
	long long kcycles;              /* kernel cycles used */
	long long pcycles;              /* process cycles used */
	unsigned int clock;             /* ms since boot */

	/* Error string for last syscall — mirrors up->errstr */
	char	errstr[256];
};

#endif /* _TOS_H_ */
