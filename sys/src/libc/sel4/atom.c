#include <u.h>
#include <libc.h>

/*
 * Compare-And-Swap (32-bit integer / uint / ulong)
 * Atomically compares *p with ov; if equal, sets *p to nv and returns 1, otherwise returns 0.
 */
int
cas32(u32int *p, u32int ov, u32int nv)
{
	return __atomic_compare_exchange_n(p, &ov, nv, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
}

int
cas(int *p, int ov, int nv)
{
	return __atomic_compare_exchange_n(p, &ov, nv, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
}

int
casl(unsigned long *p, unsigned long ov, unsigned long nv)
{
	return __atomic_compare_exchange_n(p, &ov, nv, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
}

/*
 * Compare-And-Swap (Pointer / 64-bit under LP64 GCC)
 */
int
casp(void **p, void *ov, void *nv)
{
	return __atomic_compare_exchange_n(p, &ov, nv, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
}

/*
 * Atomic Increment: increments *p and returns the NEW value.
 */
long
ainc(long *p)
{
	return __atomic_add_fetch((int *)p, 1, __ATOMIC_SEQ_CST);
}

void
_xinc(long *p)
{
	__atomic_add_fetch((int *)p, 1, __ATOMIC_SEQ_CST);
}

/*
 * Atomic Decrement: decrements *p and returns the NEW value.
 */
long
adec(long *p)
{
	return __atomic_sub_fetch((int *)p, 1, __ATOMIC_SEQ_CST);
}

long
_xdec(long *p)
{
	return __atomic_sub_fetch((int *)p, 1, __ATOMIC_SEQ_CST);
}

/* 
 * ─────────────────────────────────────────────────────────────────────────────
 * Modern Plan 9 userspace atomic wrappers prefixed with 'a' (Along / Aptr)
 * ─────────────────────────────────────────────────────────────────────────────
 */

long
agetl(Along *p)
{
	return __atomic_load_n(&p->v, __ATOMIC_SEQ_CST);
}

void*
agetp(Aptr *p)
{
	return __atomic_load_n(&p->v, __ATOMIC_SEQ_CST);
}

long
aswapl(Along *p, long v)
{
	return __atomic_exchange_n(&p->v, v, __ATOMIC_SEQ_CST);
}

void*
aswapp(Aptr *p, void *v)
{
	return __atomic_exchange_n(&p->v, v, __ATOMIC_SEQ_CST);
}

long
aincl(Along *p, long dv)
{
	return __atomic_add_fetch(&p->v, dv, __ATOMIC_SEQ_CST);
}

int
acasl(Along *p, long ov, long nv)
{
	return __atomic_compare_exchange_n(&p->v, &ov, nv, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
}

int
acasp(Aptr *p, void *ov, void *nv)
{
	return __atomic_compare_exchange_n(&p->v, &ov, nv, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
}

void
coherence(void)
{
	__atomic_thread_fence(__ATOMIC_SEQ_CST);
}
