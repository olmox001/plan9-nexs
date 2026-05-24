#include <u.h>
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "../port/error.h"

/* Simple global pointers for CPU structure and active process */
Mach* m = nil;
Proc* up = nil;

/* Static instances of CPU 0 structure and system configuration */
static Mach mach0;
Conf conf;

/* Cooperative interrupt flag */
static int irq_state = 1;

int
splhi(void)
{
	int old = irq_state;
	irq_state = 0;
	return old;
}

int
spllo(void)
{
	int old = irq_state;
	irq_state = 1;
	return old;
}

void
splx(int state)
{
	irq_state = state;
}

int
splfhi(void)
{
	return splhi();
}

void
splflo(void)
{
	spllo();
}

void
coherence(void)
{
	__atomic_thread_fence(__ATOMIC_SEQ_CST);
}

void
idlehands(void)
{
	/* Under cooperative seL4 PD, idle simply spins or yields */
	for(int i = 0; i < 100; i++) {
		coherence();
	}
}

/* 
 * confinit: Configure the memory bounds and page pools.
 * Under seL4 Microkit, we declare a single static memory region 
 * of 32MB representing our shared memory or execution pool.
 */
void
confinit(void)
{
	/* Setup a single memory pool at base 0x10000000 of 32MB */
	conf.nmach = 1;
	conf.nproc = 64;
	
	conf.mem[0].base = 0x10000000;
	conf.mem[0].npage = (32 * MiB) / BY2PG;
	conf.mem[0].limit = conf.mem[0].base + (32 * MiB);
	conf.mem[0].kbase = conf.mem[0].base;
	conf.mem[0].klimit = conf.mem[0].limit;
	
	conf.npage = conf.mem[0].npage;
	conf.upages = (conf.npage * 60) / 100; /* 60% memory for userland */
	conf.ialloc = 1 * MiB;
	conf.pipeqsize = 32768;
}

/*
 * machinit: Initialize current processor pointer m.
 */
void
machinit(void)
{
	memset(&mach0, 0, sizeof(Mach));
	mach0.machno = 0;
	m = &mach0;
}

/*
 * meminit: direct memory initialization stub.
 */
void
meminit(void)
{
	/* Standard memory pool initialization is handled in port/alloc.c */
}

/*
 * links: Device driver linking registration stub.
 */
void
links(void)
{
	/* Manual devtab registration is handled in sel4.c */
}

/*
 * Proc life cycle and MMU stubs under cooperative single address-space PD.
 */
void
putasid(Proc *p)
{
	USED(p);
}

void
procfork(Proc *p)
{
	USED(p);
}

void
procsetup(Proc *p)
{
	USED(p);
}

void
procsave(Proc *p)
{
	USED(p);
}

void
procrestore(Proc *p)
{
	USED(p);
}

/*
 * Floating point unit stubs under cooperative EL0 Protection Domain.
 */
void
fpuinit(void)
{
}

void
fpuprocsetup(Proc *p)
{
	USED(p);
}

void
fpuprocfork(Proc *p)
{
	USED(p);
}

void
fpuprocsave(Proc *p)
{
	USED(p);
}

void
fpuprocrestore(Proc *p)
{
	USED(p);
}

/* Additional hardware stub overrides */
void
reboot(void *v1, void *v2, ulong u)
{
	USED(v1, v2, u);
	while(1);
}

void
microdelay(int us)
{
	for(volatile int i = 0; i < us * 100; i++);
}

void
delay(int ms)
{
	microdelay(ms * 1000);
}

/* 
 * Plan 9 lock implementation using custom _tas and _barrier shims.
 * Works natively for single-CPU and cooperative thread context switches.
 */
void
lock(Lock *lk)
{
	while(_tas(&lk->key) != 0) {
		coherence();
	}
}

int
canlock(Lock *lk)
{
	return _tas(&lk->key) == 0;
}

void
unlock(Lock *lk)
{
	lk->key = _barrier(0);
}

void
ilock(Lock *lk)
{
	lk->sr = splhi();
	lock(lk);
}

/* RWLock: on single-CPU cooperative PD, degrade to plain spin locks */
void
rlock(RWLock *lk)
{
	lock(&lk->use);
}

void
runlock(RWLock *lk)
{
	unlock(&lk->use);
}

int
canrlock(RWLock *lk)
{
	return canlock(&lk->use);
}

void
wlock(RWLock *lk)
{
	lock(&lk->use);
}

void
wunlock(RWLock *lk)
{
	unlock(&lk->use);
}

int
canwlock(RWLock *lk)
{
	return canlock(&lk->use);
}

void
iunlock(Lock *lk)
{
	ulong sr = lk->sr;
	unlock(lk);
	splx(sr);
}

/* Global variables for Plan 9 kernel environment */
char *eve = "root";
char *configfile = nil;
char hostdomain[64] = "sel4.plan9";

/* Time and high-resolution timer stubs */
vlong
todget(vlong *tod, vlong *v2)
{
	USED(v2);
	if(tod)
		*tod = 0;
	return 0;
}

uvlong
fastticks(uvlong *hz)
{
	if(hz)
		*hz = 1000000ULL;
	return 0;
}

ulong
tk2ms(ulong ticks)
{
	return ticks * (1000 / HZ);
}

/* Device masking stub (always enabled) */
int
devmasked(Pgrp *pgrp, int i)
{
	USED(pgrp, i);
	return 0;
}

/* Cryptographic random source stub */
ulong
randomread(void *buf, ulong n)
{
	memset(buf, 0, n);
	return n;
}

/* Minimal dummy Queue structure and stubs for Stage 1 console */
struct Queue {
	int dummy;
};

Queue*
qopen(int limit, int msg, void (*kick)(void*), void *arg)
{
	static Queue dummy_q;
	USED(limit, msg, kick, arg);
	return &dummy_q;
}

void
qnoblock(Queue *q, int onoff)
{
	USED(q, onoff);
}

void
qreopen(Queue *q)
{
	USED(q);
}

void
qhangup(Queue *q, char *msg)
{
	USED(q, msg);
}

long
qread(Queue *q, void *buf, int n)
{
	USED(q, buf, n);
	return 0;
}

int
qlen(Queue *q)
{
	USED(q);
	return 0;
}

int
qisclosed(Queue *q)
{
	USED(q);
	return 0;
}

/* Safe kernel string copy */
void
kstrcpy(char *s, char *t, int ns)
{
	if(ns <= 0)
		return;
	while(--ns > 0 && *t)
		*s++ = *t++;
	*s = '\0';
}

/* Read error string from Proc */
int
rerrstr(char *buf, uint n)
{
	if(up) {
		kstrcpy(buf, up->errstr, n);
		return 0;
	}
	kstrcpy(buf, "no error", n);
	return 0;
}

/*
 * error — Write err into up->errstr and jump back to the nearest waserror frame.
 *
 * waserror() is the macro: setlabel(&up->errlab[up->nerrlab++])
 * error()    unwinds to that frame via gotolabel.
 * poperror() is the macro: up->nerrlab--
 *
 * If up is nil or nerrlab == 0, we have an unhandled error and panic.
 */
void
error(char *err)
{
	if(up == nil)
		panic("error with nil up: %s", err);
	if(up->nerrlab <= 0)
		panic("unhandled error: %s", err);
	/* Copy error string into the current errstr buffer */
	kstrcpy(up->errstr, err, ERRMAX);
	/* Swap syserrstr and errstr as Plan 9 convention requires */
	char *tmp = up->syserrstr;
	up->syserrstr = up->errstr;
	up->errstr = tmp;
	/* Unwind to the nearest waserror frame */
	gotolabel(&up->errlab[--up->nerrlab]);
}

/*
 * nexterror — Continue unwinding to the next outer waserror frame.
 * Called from catch blocks that cannot handle the current error.
 */
void
nexterror(void)
{
	if(up == nil || up->nerrlab <= 0)
		panic("nexterror: no error frame");
	gotolabel(&up->errlab[--up->nerrlab]);
}

/* Pool memory allocator definitions and stubs */
void *mainmem;
void *imagmem;

void*
poolalloc(void *pool, ulong size)
{
	USED(pool);
	return xalloc(size);
}

void
poolfree(void *pool, void *v)
{
	USED(pool);
	xfree(v);
}

void*
poolrealloc(void *pool, void *v, ulong size)
{
	USED(pool);
	void *new_p = xalloc(size);
	if(new_p && v) {
		memmove(new_p, v, size);
		xfree(v);
	}
	return new_p;
}

ulong
poolmsize(void *pool, void *v)
{
	USED(pool, v);
	return 0;
}

void*
poolallocalign(void *pool, ulong size, ulong align, long offset, ulong span)
{
	USED(pool, align, offset, span);
	return xalloc(size);
}

void
resrcwait(char *msg)
{
	USED(msg);
}

/*
 * cankaddr — Return the number of bytes, starting from `addr`, that are
 * directly kernel-addressable.
 *
 * Under seL4 Microkit with KZERO=0 (identity mapping) our single memory
 * region is 32MB at 0x10000000. xinit() uses this to compute:
 *   maxpages = cankaddr(base) / BY2PG
 * so we must return the size of the addressable region from `addr`.
 */
int
cankaddr(uintptr addr)
{
	/* Our pool starts at 0x10000000 and is 32MB. */
	uintptr pool_base  = 0x10000000UL;
	uintptr pool_limit = pool_base + (32 * MiB);
	if(addr >= pool_limit)
		return 0;
	if(addr < pool_base)
		return (int)(pool_limit - pool_base);
	return (int)(pool_limit - addr);
}

long
seconds(void)
{
	return 0;
}

ulong kerndate = 0;

/*
 * Chan lifecycle — fully functional now that xinit() initialises the heap.
 *
 * newchan: Allocate and zero a Chan. Ref count starts at 1.
 * cclose:  Decrement ref; free when it hits 0.
 */
Chan*
newchan(void)
{
	Chan *c;
	c = (Chan*)xalloc(sizeof(Chan));
	if(c == nil)
		panic("newchan: out of memory");
	memset(c, 0, sizeof(Chan));
	c->ref_member.ref = 1;
	return c;
}

void
cclose(Chan *c)
{
	if(c == nil)
		return;
	/* Ref.ref is the long counter; we decrement directly since
	 * decref takes a Ref*, which after sanitization is ref_member */
	if(--c->ref_member.ref == 0)
		xfree(c);
}

/*
 * Path lifecycle.
 */
Path*
newpath(char *s)
{
	Path *p;
	int n;
	if(s == nil)
		s = "";
	n = strlen(s);
	p = (Path*)xalloc(sizeof(Path) + n + 1);
	if(p == nil)
		panic("newpath: out of memory");
	memset(p, 0, sizeof(Path));
	p->ref_member.ref = 1;
	p->alen = n + 1;
	p->len = n;
	p->s = (char*)(p + 1);
	memmove(p->s, s, n + 1);
	return p;
}

void
pathclose(Path *p)
{
	if(p == nil)
		return;
	if(--p->ref_member.ref == 0)
		xfree(p);
}

/*
 * isdir — verify channel is a directory; error if not.
 */
void
isdir(Chan *c)
{
	if(c->qid.type & QTDIR)
		return;
	error(Enotdir);
}

int
openmode(ulong mode)
{
	return (int)mode;
}

/* Core block subsystem stubs */
Block*
allocb(int size)
{
	USED(size);
	return nil;
}

void
freeb(Block *b)
{
	USED(b);
}

/* Core kernel iprint and write stubs for console */
int
iprint(char *fmt, ...)
{
	va_list arg;
	char buf[256];
	va_start(arg, fmt);
	vseprint(buf, buf+sizeof(buf), fmt, arg);
	va_end(arg);
	putstrn(buf, strlen(buf));
	return 0;
}

long
write(int fd, void *buf, long n)
{
	if(fd == 1 || fd == 2) {
		putstrn((char*)buf, n);
		return n;
	}
	return -1;
}

/*
 * readstr: copy string str into buf at offset off.
 * Signature matches portfns.h: int readstr(ulong, char*, ulong, char*)
 */
int
readstr(ulong off, char *buf, ulong n, char *str)
{
	ulong size;

	size = strlen(str);
	if(off >= size)
		return 0;
	if(off + n > size)
		n = size - off;
	memmove(buf, str + off, n);
	return (int)n;
}

/*
 * procfdprint: print fd info into buf (used by devdup ctl read).
 * Signature matches portfns.h: int procfdprint(Chan*, int, char*, int)
 */
int
procfdprint(Chan *c, int fd, char *buf, int n)
{
	char dc = '?';
	if(c != nil && devtab[c->type] != nil)
		dc = devtab[c->type]->dc;
	return snprint(buf, n, "fd %d type %c qid %llud\n",
		fd, dc, (uvlong)c->qid.path);
}
