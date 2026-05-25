#include <u.h>
#include <libc.h>
#include "../../9/sel4/include/tos.h"
#include "../9syscall/sys.h"

extern Tos *_tos;

/* Helper to execute a system call through the shared Tos gate */
static long
sys_call(int sysno, long a0, long a1, long a2, long a3, long a4, long a5)
{
	if(_tos == nil || _tos->gate == nil)
		return -1;
	return _tos->gate(sysno, a0, a1, a2, a3, a4, a5);
}

int
open(char *path, int mode)
{
	return sys_call(OPEN, (long)path, (long)mode, 0, 0, 0, 0);
}

int
close(int fd)
{
	return sys_call(CLOSE, (long)fd, 0, 0, 0, 0, 0);
}

long
read(int fd, void *buf, long n)
{
	/* In Plan 9, read is pread with offset -1 */
	return sys_call(PREAD, (long)fd, (long)buf, (long)n, -1LL, 0, 0);
}

long
write(int fd, void *buf, long n)
{
	/* In Plan 9, write is pwrite with offset -1 */
	return sys_call(PWRITE, (long)fd, (long)buf, (long)n, -1LL, 0, 0);
}

long
pread(int fd, void *buf, long n, vlong off)
{
	return sys_call(PREAD, (long)fd, (long)buf, (long)n, (long)off, 0, 0);
}

long
pwrite(int fd, void *buf, long n, vlong off)
{
	return sys_call(PWRITE, (long)fd, (long)buf, (long)n, (long)off, 0, 0);
}

int
create(char *path, int mode, ulong perm)
{
	return sys_call(CREATE, (long)path, (long)mode, (long)perm, 0, 0, 0);
}

int
remove(char *path)
{
	return sys_call(REMOVE, (long)path, 0, 0, 0, 0, 0);
}

int
dup(int oldfd, int newfd)
{
	return sys_call(DUP, (long)oldfd, (long)newfd, 0, 0, 0, 0);
}

int
pipe(int *fds)
{
	return sys_call(PIPE, (long)fds, 0, 0, 0, 0, 0);
}

vlong
seek(int fd, vlong off, int whence)
{
	return sys_call(SEEK, (long)fd, (long)off, (long)whence, 0, 0, 0);
}

int
stat(char *path, uchar *buf, int n)
{
	return sys_call(STAT, (long)path, (long)buf, (long)n, 0, 0, 0);
}

int
fstat(int fd, uchar *buf, int n)
{
	return sys_call(FSTAT, (long)fd, (long)buf, (long)n, 0, 0, 0);
}

int
wstat(char *path, uchar *buf, int n)
{
	return sys_call(WSTAT, (long)path, (long)buf, (long)n, 0, 0, 0);
}

int
fwstat(int fd, uchar *buf, int n)
{
	return sys_call(FWSTAT, (long)fd, (long)buf, (long)n, 0, 0, 0);
}

int
fd2path(int fd, char *buf, int n)
{
	return sys_call(FD2PATH, (long)fd, (long)buf, (long)n, 0, 0, 0);
}

int
errstr(char *buf, uint n)
{
	return sys_call(ERRSTR, (long)buf, (long)n, 0, 0, 0, 0);
}

int
chdir(char *path)
{
	return sys_call(CHDIR, (long)path, 0, 0, 0, 0, 0);
}

int
bind(char *name, char *old, int flag)
{
	return sys_call(BIND, (long)name, (long)old, (long)flag, 0, 0, 0);
}

int
mount(int fd, int afd, char *old, int flag, char *aname)
{
	return sys_call(MOUNT, (long)fd, (long)afd, (long)old, (long)flag, (long)aname, 0);
}

int
unmount(char *name, char *old)
{
	return sys_call(UNMOUNT, (long)name, (long)old, 0, 0, 0, 0);
}

void
_exits(char *msg)
{
	sys_call(EXITS, (long)msg, 0, 0, 0, 0, 0);
	while(1);
}

int
rfork(int flags)
{
	return sys_call(RFORK, (long)flags, 0, 0, 0, 0, 0);
}

int
exec(char *path, char **argv)
{
	return sys_call(EXEC, (long)path, (long)argv, 0, 0, 0, 0);
}

int
sleep(long ms)
{
	return sys_call(SLEEP, (long)ms, 0, 0, 0, 0, 0);
}

int
brk(void *addr)
{
	USED(addr);
	return 0;
}

void*
sbrk(usize n)
{
	long r;
	r = sys_call(BRK_, (long)n, 0, 0, 0, 0, 0);
	if(r == -1LL || r == 0)
		return (void*)-1LL;
	return (void*)r;
}

vlong
nsec(void)
{
	return sys_call(_NSEC, 0, 0, 0, 0, 0, 0);
}

int
notify(void (*fn)(void*, char*))
{
	return sys_call(NOTIFY, (long)fn, 0, 0, 0, 0, 0);
}

int
noted(int v)
{
	return sys_call(NOTED, (long)v, 0, 0, 0, 0, 0);
}

/* Plan 9 alarm system call wrapper */
long
alarm(ulong ms)
{
	return sys_call(ALARM, (long)ms, 0, 0, 0, 0, 0);
}

/* Plan 9 await system call wrapper */
int
await(char *buf, int n)
{
	return sys_call(AWAIT, (long)buf, (long)n, 0, 0, 0, 0);
}

int
getpid(void)
{
	return sys_call(SYSR1, 0, 0, 0, 0, 0, 0);
}

void*
rendezvous(void *tag, void *val)
{
	return (void*)sys_call(RENDEZVOUS, (long)tag, (long)val, 0, 0, 0, 0);
}
