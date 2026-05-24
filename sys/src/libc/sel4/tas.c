#include <u.h>
#include <libc.h>

/*
 * _tas: Test-And-Set lock.
 * Atomically exchanges the value at *addr with 1 and returns the old value.
 * Uses GCC's standard __atomic_exchange_n for correct barrier semantics on AArch64.
 */
int
_tas(int *addr)
{
	return __atomic_exchange_n(addr, 1, __ATOMIC_SEQ_CST);
}

/*
 * _barrier: Data memory barrier.
 * Generates a full sequential consistency memory fence to serialize memory accesses.
 */
uintptr
_barrier(uintptr val)
{
	__atomic_thread_fence(__ATOMIC_SEQ_CST);
	return val;
}
