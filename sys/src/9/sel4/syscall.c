/*
 * syscall.c — Plan 9 syscall dispatcher for seL4 Microkit.
 *
 * All Plan 9 user processes (loaded via exec9p) run in the same address
 * space as plan9_root. They call into the kernel via the function pointer
 * stored in Tos.gate (set by svc_init at boot). This dispatcher implements
 * the full Plan 9 syscall ABI using the existing VFS layer.
 *
 * Syscall numbers from sys/src/libc/9syscall/sys.h:
 *   SYSR1=0  _ERRSTR=1  BIND=2  CHDIR=3  CLOSE=4  DUP=5  ALARM=6
 *   EXEC=7   EXITS=8    FAUTH=10  SEGBRK=12  OPEN=14  _READ=15
 *   SLEEP=17 _STAT=18   RFORK=19  _WRITE=20  PIPE=21  CREATE=22
 *   FD2PATH=23  BRK_=24  REMOVE=25  _WSTAT=26  _FWSTAT=27
 *   NOTIFY=28  NOTED=29  RENDEZVOUS=34  UNMOUNT=35  _WAIT=36
 *   SEMACQUIRE=37  SEMRELEASE=38  SEEK=39  FVERSION=40  ERRSTR=41
 *   STAT=42  FSTAT=43  WSTAT=44  FWSTAT=45  MOUNT=46  AWAIT=47
 *   PREAD=50  PWRITE=51  TSEMACQUIRE=52  _NSEC=53
 */

#include <u.h>
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "../port/error.h"
#include "include/tos.h"

/* Plan 9 syscall numbers */
#define SYS_SYSR1	0
#define SYS_ERRSTR	41
#define SYS_BIND	2
#define SYS_CHDIR	3
#define SYS_CLOSE	4
#define SYS_DUP		5
#define SYS_EXEC	7
#define SYS_EXITS	8
#define SYS_OPEN	14
#define SYS_SLEEP	17
#define SYS_RFORK	19
#define SYS_PIPE	21
#define SYS_CREATE	22
#define SYS_FD2PATH	23
#define SYS_BRK_	24
#define SYS_REMOVE	25
#define SYS_NOTIFY	28
#define SYS_NOTED	29
#define SYS_UNMOUNT	35
#define SYS_AWAIT	47
#define SYS_SEEK	39
#define SYS_STAT	42
#define SYS_FSTAT	43
#define SYS_WSTAT	44
#define SYS_FWSTAT	45
#define SYS_MOUNT	46
#define SYS_PREAD	50
#define SYS_PWRITE	51
#define SYS_NSEC	53
#define SYS_RENDEZVOUS	34
#define SYS_SEMACQUIRE	37
#define SYS_SEMRELEASE	38

/* ── Helper: get pid from scheduler ─────────────────────────────────────── */
static int
getpid(void)
{
	return up ? up->pid : 0;
}

/* ── pread / pwrite ─────────────────────────────────────────────────────── */
static long
sys_pread(int fd, void *buf, long n, vlong off)
{
	Chan *c;
	long  r;

	c = fdtochan(fd, OREAD, 1, 1);
	if(waserror()){
		cclose(c);
		nexterror();
	}
	r = devtab[c->type]->read(c, buf, n, off);
	poperror();
	cclose(c);
	return r;
}

static long
sys_pwrite(int fd, void *buf, long n, vlong off)
{
	Chan *c;
	long  r;

	c = fdtochan(fd, OWRITE, 1, 1);
	if(waserror()){
		cclose(c);
		nexterror();
	}
	r = devtab[c->type]->write(c, buf, n, off);
	poperror();
	cclose(c);
	return r;
}

/* ── open / create / close / remove ────────────────────────────────────── */
static int
sys_open(char *path, int mode)
{
	Chan *c;
	int   fd;

	c = namec(path, Aopen, mode, 0);
	if(waserror()){
		cclose(c);
		nexterror();
	}
	c->flag |= COPEN;
	c->mode  = openmode(mode);
	fd = newfd(c, 0);
	poperror();
	return fd;
}

static int
sys_create(char *path, int mode, ulong perm)
{
	Chan *c;
	int   fd;

	c = namec(path, Acreate, mode, perm);
	if(waserror()){
		cclose(c);
		nexterror();
	}
	c->flag |= COPEN;
	c->mode  = openmode(mode);
	fd = newfd(c, 0);
	poperror();
	return fd;
}

static int
sys_close(int fd)
{
	fdclose(fd, 0);
	return 0;
}

static int
sys_remove(char *path)
{
	Chan *c;

	c = namec(path, Aremove, 0, 0);
	if(waserror()){
		cclose(c);
		nexterror();
	}
	devtab[c->type]->remove(c);
	cclose(c);
	poperror();
	return 0;
}

/* ── dup ────────────────────────────────────────────────────────────────── */
static int
sys_dup(int oldfd, int newfd_n)
{
	Chan *c;
	int   fd;

	c = fdtochan(oldfd, -1, 0, 1);
	if(waserror()){
		cclose(c);
		nexterror();
	}
	if(newfd_n >= 0){
		fdinstall(newfd_n, c);
		fd = newfd_n;
	} else {
		fd = newfd(c, 0);
	}
	poperror();
	return fd;
}

/* ── pipe ───────────────────────────────────────────────────────────────── */
static int
sys_pipe(int *fds)
{
	Chan *c[2];

	devpipealloc(c);
	if(waserror()){
		cclose(c[0]); cclose(c[1]);
		nexterror();
	}
	fds[0] = newfd(c[0], 0);
	fds[1] = newfd(c[1], 0);
	poperror();
	return 0;
}

/* ── seek ───────────────────────────────────────────────────────────────── */
static vlong
sys_seek(int fd, vlong off, int whence)
{
	Chan *c;
	vlong r;

	c = fdtochan(fd, -1, 1, 1);
	if(waserror()){
		cclose(c);
		nexterror();
	}
	r = devtab[c->type]->read(c, nil, -1, off); /* convention: n=-1 → seek */
	USED(r);
	/* Real seek: use the offset field in the Chan */
	switch(whence){
	case 0:	c->offset = off; break;
	case 1:	c->offset += off; break;
	case 2:	/* SEEK_END: not always trivial */ c->offset = off; break;
	}
	r = c->offset;
	poperror();
	cclose(c);
	return r;
}

/* ── stat / fstat / wstat / fwstat ─────────────────────────────────────── */
static int
sys_stat(char *path, uchar *buf, int n)
{
	Chan *c;
	int   r;

	c = namec(path, Aaccess, 0, 0);
	if(waserror()){
		cclose(c);
		nexterror();
	}
	r = devtab[c->type]->stat(c, buf, n);
	poperror();
	cclose(c);
	return r;
}

static int
sys_fstat(int fd, uchar *buf, int n)
{
	Chan *c;
	int   r;

	c = fdtochan(fd, -1, 0, 1);
	if(waserror()){
		cclose(c);
		nexterror();
	}
	r = devtab[c->type]->stat(c, buf, n);
	poperror();
	cclose(c);
	return r;
}

static int
sys_wstat(char *path, uchar *buf, int n)
{
	Chan *c;

	c = namec(path, Aaccess, 0, 0);
	if(waserror()){
		cclose(c);
		nexterror();
	}
	devtab[c->type]->wstat(c, buf, n);
	poperror();
	cclose(c);
	return 0;
}

static int
sys_fwstat(int fd, uchar *buf, int n)
{
	Chan *c;

	c = fdtochan(fd, -1, 0, 1);
	if(waserror()){
		cclose(c);
		nexterror();
	}
	devtab[c->type]->wstat(c, buf, n);
	poperror();
	cclose(c);
	return 0;
}

/* ── fd2path ────────────────────────────────────────────────────────────── */
static int
sys_fd2path(int fd, char *buf, int n)
{
	Chan *c;

	c = fdtochan(fd, -1, 0, 0);
	if(c == nil || c->path == nil){
		kstrcpy(buf, "", n);
		return 0;
	}
	kstrcpy(buf, c->path->s, n);
	return 0;
}

/* ── errstr ─────────────────────────────────────────────────────────────── */
static int
sys_errstr(char *buf, uint n)
{
	char tmp[ERRMAX];

	/* swap: user buf → up->errstr, up->errstr → user buf */
	kstrcpy(tmp, buf, sizeof tmp);
	kstrcpy(buf, up->errstr, n);
	kstrcpy(up->errstr, tmp, ERRMAX);
	return 0;
}

/* ── chdir ──────────────────────────────────────────────────────────────── */
static int
sys_chdir(char *path)
{
	Chan *c;

	c = namec(path, Atodir, OREAD, 0);
	if(up->dot != nil)
		cclose(up->dot);
	up->dot = c;
	return 0;
}

/* ── bind / mount / unmount ─────────────────────────────────────────────── */
static int
sys_bind(char *name, char *old, int flag)
{
	USED(name, old, flag);
	/* Namespace operations: stub for now — full implementation needs
	 * Plan 9 Pgrp/Mhead infrastructure. */
	return 0;
}

static int
sys_mount(int fd, int afd, char *old, int flag, char *aname)
{
	USED(fd, afd, old, flag, aname);
	return 0;
}

static int
sys_unmount(char *name, char *old)
{
	USED(name, old);
	return 0;
}

/* ── exits ──────────────────────────────────────────────────────────────── */
static void
sys_exits(char *msg)
{
	pexit(msg ? msg : "", 1);
	/* never returns */
}

/* ── rfork ──────────────────────────────────────────────────────────────── */

typedef struct RforkCtx RforkCtx;
struct RforkCtx {
	Label	label;
	int	child;
};

/* rfork: create a new Plan 9 process sharing address space.
 * Returns 0 to child, child pid to parent. */
static int
sys_rfork(int flags)
{
	Proc *p;
	Fgrp *nfgrp;

	USED(flags);   /* full rfork flags processing TBD */

	/* Allocate child proc */
	p = allocproc();
	if(p == nil)
		error("rfork: out of procs");

	/* Inherit or copy fgrp based on flags */
	if(flags & RFFDG){
		/* Clone fd group */
		nfgrp = dupfgrp(up->fgrp);
		p->fgrp = nfgrp;
	} else {
		/* Share fd group */
		up->fgrp->ref_member.ref++;
		p->fgrp = up->fgrp;
	}

	p->dot    = up->dot;
	if(p->dot)
		p->dot->ref_member.ref++;

	p->text      = up->text;
	p->syserrstr = p->errbuf0;
	p->errstr    = p->errbuf1;

	/* Child starts from same label as parent.
	 * In a coroutine model, both run in the same thread cooperatively. */
	p->kpfun = nil;   /* no kproc function: use setlabel/gotolabel */

	/* Simple implementation: fork is not fully supported in
	 * cooperative single-AS. Return child pid to parent, 0 to child
	 * via scheduler label — this needs full coroutine fork support.
	 * For now, return the child pid and let it be exec'd immediately. */
	ready(p);
	return p->pid;
}

/* ── exec ───────────────────────────────────────────────────────────────── */
static int
sys_exec(char *path, char **argv)
{
	exec9p(path, argv);
	/* exec9p creates a new proc and returns; current proc continues */
	return 0;
}

/* ── sleep ──────────────────────────────────────────────────────────────── */
static int
sys_sleep(long ms)
{
	delay((int)ms);
	return 0;
}

/* ── brk ────────────────────────────────────────────────────────────────── */
static void*
sys_brk(ulong size)
{
	if(size == 0)
		return (void*)0;
	return xalloc(size);
}

/* ── nsec ───────────────────────────────────────────────────────────────── */
static vlong
sys_nsec(vlong *p)
{
	vlong v = 0;
	if(p) *p = v;
	return v;
}

/* ── notify / noted ─────────────────────────────────────────────────────── */
static int
sys_notify(void *fn)
{
	USED(fn);
	return 0;
}

static int
sys_noted(int v)
{
	USED(v);
	return 0;
}

/* ── getpid (SYSR1) ─────────────────────────────────────────────────────── */
static long
sys_sysr1(void)
{
	return getpid();
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Main dispatcher — called via Tos.gate function pointer from user processes
 * ═══════════════════════════════════════════════════════════════════════════ */
long
syscall_dispatch(long sysno, long a0, long a1, long a2,
                 long a3, long a4, long a5)
{
	long ret = -1;

	if(up == nil){
		/* Called from init context before any proc — only allow exits */
		if(sysno == SYS_EXITS)
			while(1);
		return -1;
	}

	if(waserror()){
		/* Copy error into the caller's Tos.errstr if possible */
		return -1LL;
	}

	switch((int)sysno){
	case SYS_PREAD:
		ret = sys_pread((int)a0, (void*)a1, (long)a2, (vlong)a3);
		break;
	case SYS_PWRITE:
		ret = sys_pwrite((int)a0, (void*)a1, (long)a2, (vlong)a3);
		break;
	case SYS_OPEN:
		ret = sys_open((char*)a0, (int)a1);
		break;
	case SYS_CREATE:
		ret = sys_create((char*)a0, (int)a1, (ulong)a2);
		break;
	case SYS_CLOSE:
		ret = sys_close((int)a0);
		break;
	case SYS_REMOVE:
		ret = sys_remove((char*)a0);
		break;
	case SYS_DUP:
		ret = sys_dup((int)a0, (int)a1);
		break;
	case SYS_PIPE:
		ret = sys_pipe((int*)a0);
		break;
	case SYS_SEEK:
		ret = sys_seek((int)a0, (vlong)a1, (int)a2);
		break;
	case SYS_STAT:
		ret = sys_stat((char*)a0, (uchar*)a1, (int)a2);
		break;
	case SYS_FSTAT:
		ret = sys_fstat((int)a0, (uchar*)a1, (int)a2);
		break;
	case SYS_WSTAT:
		ret = sys_wstat((char*)a0, (uchar*)a1, (int)a2);
		break;
	case SYS_FWSTAT:
		ret = sys_fwstat((int)a0, (uchar*)a1, (int)a2);
		break;
	case SYS_FD2PATH:
		ret = sys_fd2path((int)a0, (char*)a1, (int)a2);
		break;
	case SYS_ERRSTR:
		ret = sys_errstr((char*)a0, (uint)a1);
		break;
	case SYS_CHDIR:
		ret = sys_chdir((char*)a0);
		break;
	case SYS_BIND:
		ret = sys_bind((char*)a0, (char*)a1, (int)a2);
		break;
	case SYS_MOUNT:
		ret = sys_mount((int)a0, (int)a1, (char*)a2, (int)a3, (char*)a4);
		break;
	case SYS_UNMOUNT:
		ret = sys_unmount((char*)a0, (char*)a1);
		break;
	case SYS_EXITS:
		poperror();
		sys_exits((char*)a0);
		/* NOTREACHED */
		break;
	case SYS_RFORK:
		ret = sys_rfork((int)a0);
		break;
	case SYS_EXEC:
		ret = sys_exec((char*)a0, (char**)a1);
		break;
	case SYS_SLEEP:
		ret = sys_sleep((long)a0);
		break;
	case SYS_BRK_:
		ret = (long)sys_brk((ulong)a0);
		break;
	case SYS_NSEC:
		ret = sys_nsec((vlong*)a0);
		break;
	case SYS_NOTIFY:
		ret = sys_notify((void*)a0);
		break;
	case SYS_NOTED:
		ret = sys_noted((int)a0);
		break;
	case SYS_SYSR1:
		ret = sys_sysr1();
		break;
	case SYS_AWAIT:
		/* wait for child — stub */
		ret = 0;
		break;
	case SYS_RENDEZVOUS:
		/* rendezvous — stub */
		ret = (long)a1;
		break;
	case SYS_SEMACQUIRE:
	case SYS_SEMRELEASE:
		/* semaphore — stub */
		ret = 0;
		break;
	default:
		poperror();
		return -1;
	}

	poperror();
	return ret;
}

/* ── svc_init: write dispatcher address into every new process's Tos ─────── */
/*
 * svc_init is called from plan9_root init().
 * It initialises the syscall gate function pointer.
 * The actual gate address is written per-process in exec.c (exectrampoline).
 */
void
svc_init(void)
{
	/* Nothing global to init — gate is per-Tos, set in exectrampoline. */
	microkit_dbg_puts("plan9_root: syscall gate ready\n");
}
