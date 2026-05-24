#ifndef _SEL4_DAT_H_
#define _SEL4_DAT_H_

typedef struct Confmem Confmem;
typedef struct Conf Conf;
typedef struct MMMU MMMU;
typedef struct PMMU PMMU;
typedef struct Mach Mach;
typedef struct FPsave FPsave;
typedef struct FPalloc FPalloc;
typedef struct Page Page;

/* Native typedefs for anonymous embedding under -fms-extensions */
typedef struct PFPU PFPU;
typedef struct PMMU PMMU;

typedef uvlong Tval;

#define MAXSYSARG	5

struct FPsave
{
	uvlong	regs[32][2];
	ulong	control;
	ulong	status;
};

struct FPalloc
{
	FPsave;
	FPalloc	*link;
};

#define KFPSTATE

struct PFPU
{
	int	fpstate;
	int	kfpstate;
	FPalloc	*fpsave;
	FPalloc	*kfpsave;
};

enum
{
	FPinit,
	FPactive,
	FPprotected,
	FPinactive,
	FPnotify = 0x100,
};

struct Confmem
{
	uintptr	base;
	ulong	npage;
	uintptr	limit;
	uintptr	kbase;
	uintptr	klimit;
};

struct Conf
{
	ulong	nmach;		/* processors */
	ulong	nproc;		/* processes */
	Confmem	mem[3];		/* physical memory */
	ulong	npage;		/* total physical pages of memory */
	ulong	upages;		/* user page pool */
	ulong	copymode;	/* 0 is copy on write, 1 is copy on reference */
	ulong	ialloc;		/* max interrupt time allocation in bytes */
	ulong	pipeqsize;	/* size in bytes of pipe queues */
	ulong	nimage;		/* number of page cache image headers */
	ulong	nswap;		/* number of swap pages */
	int	nswppo;		/* max # of pageouts per segment pass */
	int	monitor;	/* flag */
};

typedef uintptr PTE;

struct MMMU
{
	PTE*	mmutop;
};

struct PMMU
{
	union {
		Page	*mmufree;
		Page	*mmuhead[4]; /* Ptlevels = 4 on ARM64 */
	};
	Page	*mmutail[4];
	int	asid;
	uintptr	tpidr;
};

/* 
 * Include the sanitized portdat.h from the build directory
 * to avoid duplicate member conflicts.
 */
#include "portdat.h"

struct Mach
{
	int	machno;			/* physical id of processor */
	uintptr	splpc;			/* pc of last caller to splhi */
	Proc*	proc;			/* current process on this processor */

	/* 
	 * Use explicit struct tags for anonymous embedding under -fms-extensions.
	 */
	struct MMMU;
	struct PMach;

	int	fpstate;
	FPalloc	*fpsave;

	int	cputype;
	ulong	delayloop;

	int	stack[1];
};

struct
{
	char	machs[MAXMACH];		/* active CPUs */
	int	exiting;		/* shutdown */
}active;

/* 
 * Simple global pointers for m (mach) and up (proc) under GCC/Clang.
 * This completely avoids the register keywords of kencc.
 */
extern Mach* m;
extern Proc* up;

/* CPU Mach pointer lookup macro */
#define MACHP(n)	m

/* Clock frequency */
#define HZ			100

/* Map kernel lock atomic primitive */
extern int _tas(ulong*);
#define tas(x)		_tas((ulong*)(x))

/* ── 9P seL4 Shared Memory Transport Definitions ─────────────────────────── */
#define SHARE9P_BASE	0x20000000ULL
#define SHARE9P_CH	1

typedef struct Ring9P Ring9P;
struct Ring9P {
	volatile uint	w;
	volatile uint	r;
	volatile uchar	data[32760];
};

typedef struct Shared9P Shared9P;
struct Shared9P {
	Ring9P	tx;	/* Client -> Server (T-messages) */
	Ring9P	rx;	/* Server -> Client (R-messages) */
};

#endif /* _SEL4_DAT_H_ */
