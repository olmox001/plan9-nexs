/*
 * sched.c — Cooperative process scheduler for Plan 9 on seL4 Microkit.
 *
 * Single-CPU, no-preemption scheduling inside a single Protection Domain.
 * Each Proc has SEL4_KSTACK bytes of stack allocated from the kernel heap.
 * Context switch: setlabel/gotolabel saves/restores callee-saved regs+SP+LR.
 *
 * Invariant: schedinit() owns control when up==nil.  A running proc owns
 * control when up!=nil.  sched() bridges the two by saving the proc's
 * Label and jumping to m->sched (schedinit's known return point).
 */

#include <u.h>
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "../port/error.h"

#define SEL4_KSTACK  (64 * KiB)
#define MAXPROCS     128

static Proc  proctable[MAXPROCS];
static ulong nextpid   = 1;
static int   nproc     = 0;

/* Simple single-priority FIFO run queue */
static struct {
	Lock;
	Proc *head;
	Proc *tail;
	int   n;
} sq;

static void
sq_push(Proc *p)
{
	lock(&sq);
	p->rnext = nil;
	if(sq.tail != nil)
		sq.tail->rnext = p;
	else
		sq.head = p;
	sq.tail = p;
	sq.n++;
	unlock(&sq);
}

static Proc*
sq_pop(void)
{
	Proc *p;

	lock(&sq);
	p = sq.head;
	if(p != nil) {
		sq.head = p->rnext;
		if(sq.head == nil)
			sq.tail = nil;
		sq.n--;
		p->rnext = nil;
	}
	unlock(&sq);
	return p;
}

/* proctrampoline: first landing point for a brand-new proc. */
static void
proctrampoline(void)
{
	void (*fn)(void*);
	void *arg;

	fn  = up->kpfun;
	arg = up->kparg;
	(*fn)(arg);
	pexit(up->text, 1);
}

void
procinit(void)
{
	int i;

	memset(proctable, 0, sizeof(proctable));
	for(i = 0; i < MAXPROCS; i++)
		proctable[i].state = Dead;
	memset(&sq, 0, sizeof(sq));
}

Proc*
allocproc(void)
{
	Proc   *p;
	uchar  *stk;
	uintptr *tos;
	int     i;

	for(i = 0; i < MAXPROCS; i++) {
		p = &proctable[i];
		if(p->state != Dead)
			continue;
		memset(p, 0, sizeof(Proc));
		stk = (uchar*)xalloc(SEL4_KSTACK);
		if(stk == nil)
			return nil;
		p->state     = New;
		p->pid       = nextpid++;
		p->parentpid = up ? up->pid : 0;
		p->parent    = up;
		p->user      = "root";
		p->syserrstr = p->errbuf0;
		p->errstr    = p->errbuf1;
		/* Initial Label: SP at TOS, PC at trampoline */
		tos = (uintptr*)(stk + SEL4_KSTACK);
		tos = (uintptr*)STACKALIGN((uintptr)tos);
		memset(p->sched.regs, 0, sizeof(p->sched.regs));
		p->sched.regs[JMPBUFPC] = (uintptr)proctrampoline;
		p->sched.regs[JMPBUFSP] = (uintptr)tos;
		nproc++;
		return p;
	}
	return nil;
}

void
kproc(char *name, void (*fn)(void*), void *arg)
{
	Proc *p;

	p = allocproc();
	if(p == nil)
		panic("kproc: out of procs for %s", name);
	kstrcpy(p->text, name, sizeof(p->text));
	p->kpfun = fn;
	p->kparg = arg;
	p->kp    = 1;
	ready(p);
}

void
kprocchild(Proc *p, void (*fn)(void))
{
	USED(p);
	fn();
	pexit("done", 0);
}

void
ready(Proc *p)
{
	p->state = Ready;
	p->mach  = nil;
	sq_push(p);
}

Proc*
runproc(void)
{
	return sq_pop();
}

int
anyready(void)
{
	return sq.n > 0;
}

/*
 * schedinit: The machine scheduler entry point.
 * setlabel(&m->sched) is the global return address for all context switches.
 * When a proc calls sched() and does gotolabel(&m->sched), we land here.
 */
_Noreturn void
schedinit(void)
{
	Proc *p;

	setlabel(&m->sched);

	/* Handle proc that just yielded, blocked, or died */
	if(up != nil) {
		switch(up->state) {
		case Running:
			up->state = Ready;
			sq_push(up);
			break;
		case Moribund:
			up->state = Dead;
			nproc--;
			break;
		case Wakeme:
			/* sleep() already set state — do not re-queue */
			break;
		default:
			break;
		}
		up->mach = nil;
		up       = nil;
		m->proc  = nil;
	}

	/* Idle until a proc becomes available */
	while((p = sq_pop()) == nil)
		coherence();

	up        = p;
	m->proc   = up;
	up->mach  = m;
	up->state = Running;
	gotolabel(&up->sched);
	panic("schedinit: gotolabel returned");
}

/*
 * sched: Voluntary yield by a running proc.
 * Saves up->sched and jumps to the scheduler.
 * Returns when this proc is picked again.
 */
void
sched(void)
{
	if(up == nil)
		return;
	if(!setlabel(&up->sched)) {
		up->mach = nil;
		gotolabel(&m->sched);
	}
	/* Re-scheduled: restore running state */
	up->mach  = m;
	up->state = Running;
}

/*
 * sleep: Block until condition f(arg) becomes true or wakeup() is called.
 * Must not be called while holding a spin-lock with ilockdepth > 0.
 */
void
sleep(Rendez *r, int (*f)(void*), void *arg)
{
	int s;

	s = splhi();
	lock(&r->lock_member);
	lock(&up->rlock);

	r->p = up;
	if((*f)(arg)) {
		/* Condition already satisfied */
		r->p = nil;
		unlock(&up->rlock);
		unlock(&r->lock_member);
		splx(s);
		return;
	}

	up->state = Wakeme;
	up->r     = r;
	unlock(&up->rlock);
	unlock(&r->lock_member);

	if(!setlabel(&up->sched)) {
		up->mach = nil;
		gotolabel(&m->sched);
	}
	/* Woken up */
	up->mach  = m;
	up->state = Running;
	splx(s);
}

/*
 * wakeup: Wake a proc sleeping on a Rendez.
 * Returns the woken proc, or nil if nobody was sleeping.
 */
Proc*
wakeup(Rendez *r)
{
	Proc *p;
	int   s;

	s = splhi();
	lock(&r->lock_member);
	p = r->p;
	if(p != nil) {
		lock(&p->rlock);
		r->p = nil;
		p->r = nil;
		unlock(&p->rlock);
		unlock(&r->lock_member);
		ready(p);
	} else {
		unlock(&r->lock_member);
	}
	splx(s);
	return p;
}

void
tsleep(Rendez *r, int (*fn)(void*), void *arg, ulong ms)
{
	/* Simplified: busy-wait then check condition */
	ulong deadline;

	USED(r);
	deadline = ms * 1000;
	while(deadline-- > 0 && !(*fn)(arg))
		coherence();
	USED(fn); USED(arg);
}

void
pexit(char *reason, int freemem)
{
	USED(reason, freemem);
	up->state = Moribund;
	sched();
	panic("pexit: returned");
}

void
resched(char *why)
{
	USED(why);
	sched();
}

void
procinit0(void)
{
	procinit();
}

void
hzsched(void)
{
	/* No preemption — nothing to do */
}

void
mmurelease(Proc *p)
{
	USED(p);
	/* Identity mapping: no per-proc MMU state */
}

void
mmuswitch(Proc *p)
{
	USED(p);
}
