#include <u.h>
#include <libc.h>

/*
 * getcallerpc: Returns the PC of the calling function.
 * This is used for debug tagging and memory leak tracing in setmalloctag.
 * Standard GCC builtin return address returns the exact address.
 */
uintptr
getcallerpc(void *arg)
{
	(void)arg;
	return (uintptr)__builtin_return_address(0);
}
