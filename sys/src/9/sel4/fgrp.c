/*
 * fgrp.c — File-descriptor group management for Plan 9 on seL4 Microkit.
 *
 * Each Plan 9 process has an Fgrp (file group) that maps integer file
 * descriptors to Chan pointers.  Fgrp is reference-counted so that
 * fork can share it (copy-on-write semantics are not needed here because
 * we start with a simple non-forking model).
 *
 * allocfgrp()   — create a new empty Fgrp
 * closefgrp()   — decrement reference; free when zero
 * fdtochan()    — validate fd and return the Chan (with optional iref)
 * newfd()       — allocate the lowest free fd number and install a Chan
 * fdclose()     — remove an fd from the group
 */

#include <u.h>
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "../port/error.h"

enum {
	FGINIT  = 8,    /* initial fd[] array capacity */
	FGMAX   = 1024, /* maximum fds per process */
};

Fgrp*
allocfgrp(void)
{
	Fgrp *f;

	f = (Fgrp*)xalloc(sizeof(Fgrp));
	if(f == nil)
		panic("allocfgrp: out of memory");
	memset(f, 0, sizeof(Fgrp));
	f->ref_member.ref = 1;
	f->fd  = (Chan**)xalloc(FGINIT * sizeof(Chan*));
	if(f->fd == nil)
		panic("allocfgrp: out of memory for fd table");
	memset(f->fd, 0, FGINIT * sizeof(Chan*));
	f->nfd   = FGINIT;
	f->maxfd = -1;
	return f;
}

/* closefgrp: decrement reference; close all chans and free when hits 0. */
void
closefgrp(Fgrp *f)
{
	int i;

	if(f == nil)
		return;
	if(--f->ref_member.ref > 0)
		return;
	for(i = 0; i <= f->maxfd; i++) {
		if(f->fd[i] != nil) {
			cclose(f->fd[i]);
			f->fd[i] = nil;
		}
	}
	xfree(f->fd);
	xfree(f);
}

void
forceclosefgrp(void)
{
	if(up != nil && up->closingfgrp != nil) {
		closefgrp(up->closingfgrp);
		up->closingfgrp = nil;
	}
}

/*
 * fgrowfd: grow the fd[] array to fit at least newfd.
 * Called with f->lock_member held.
 */
static void
fgrowfd(Fgrp *f, int newfd)
{
	Chan **newt;
	int    n;

	if(newfd < f->nfd)
		return;
	n = f->nfd;
	while(n <= newfd)
		n *= 2;
	if(n > FGMAX)
		n = FGMAX;
	if(newfd >= n)
		error("too many open files");
	newt = (Chan**)xalloc(n * sizeof(Chan*));
	if(newt == nil)
		error("fd table: out of memory");
	memmove(newt, f->fd, f->nfd * sizeof(Chan*));
	memset(newt + f->nfd, 0, (n - f->nfd) * sizeof(Chan*));
	xfree(f->fd);
	f->fd  = newt;
	f->nfd = n;
}

/*
 * fdtochan: return Chan for file descriptor fd.
 *   mode   — required open mode (or -1 to skip mode check)
 *   chkmnt — check CMSG flag (not used in our simplified model)
 *   iref   — if non-zero, increment Chan ref count
 */
Chan*
fdtochan(int fd, int mode, int chkmnt, int iref)
{
	Fgrp *f;
	Chan *c;

	if(up == nil || up->fgrp == nil)
		error("no fgrp");
	f = up->fgrp;

	if(fd < 0 || fd > f->maxfd)
		error(Ebadfd);

	lock(&f->lock_member);
	c = f->fd[fd];
	if(c == nil) {
		unlock(&f->lock_member);
		error(Ebadfd);
	}
	if(iref)
		c->ref_member.ref++;
	unlock(&f->lock_member);

	if(mode != -1) {
		if((c->flag & COPEN) == 0)
			error("file not open");
		if(chkmnt && (c->flag & CMSG))
			error(Ebadusefd);
		if((mode & ~OEXCL) == OREAD && (c->mode & 3) == OREAD)
			goto ok;
		if((mode & ~OEXCL) == OWRITE && (c->mode & 3) == OWRITE)
			goto ok;
		if((mode & ~OEXCL) == ORDWR)
			goto ok;
		if((c->mode & 3) == ORDWR)	/* ORDWR channel permits any access mode */
			goto ok;
		/* Mode mismatch */
		if(iref)
			cclose(c);
		error(Ebadusefd);
	}
ok:
	return c;
}

/*
 * newfd: install Chan c at the lowest available fd >= over.
 * Returns the fd number, or -1 on error.
 */
int
newfd(Chan *c, int over)
{
	Fgrp *f;
	int   fd;

	if(up == nil || up->fgrp == nil)
		error("no fgrp");
	f = up->fgrp;

	lock(&f->lock_member);
	for(fd = over; fd < FGMAX; fd++) {
		if(fd >= f->nfd) {
			if(waserror()) {
				unlock(&f->lock_member);
				nexterror();
			}
			fgrowfd(f, fd);
			poperror();
		}
		if(f->fd[fd] == nil) {
			f->fd[fd] = c;
			if(fd > f->maxfd)
				f->maxfd = fd;
			unlock(&f->lock_member);
			return fd;
		}
	}
	unlock(&f->lock_member);
	error("out of file descriptors");
	return -1;   /* unreachable */
}

/*
 * fdclose: close fd and remove from group.
 * flag is unused (reserved for Plan 9 CCEXEC).
 */
void
fdclose(int fd, int flag)
{
	Fgrp *f;
	Chan *c;

	USED(flag);
	if(up == nil || up->fgrp == nil)
		return;
	f = up->fgrp;

	lock(&f->lock_member);
	if(fd < 0 || fd > f->maxfd || f->fd[fd] == nil) {
		unlock(&f->lock_member);
		return;
	}
	c       = f->fd[fd];
	f->fd[fd] = nil;
	if(fd == f->maxfd) {
		while(f->maxfd > 0 && f->fd[f->maxfd] == nil)
			f->maxfd--;
	}
	unlock(&f->lock_member);
	cclose(c);
}

/* dupfgrp: return a new Fgrp that shares the same channels (adds ref). */
Fgrp*
dupfgrp(Fgrp *f)
{
	Fgrp *nf;
	int   i;

	nf = allocfgrp();
	lock(&f->lock_member);
	for(i = 0; i <= f->maxfd; i++) {
		if(f->fd[i] == nil)
			continue;
		if(i >= nf->nfd) {
			if(waserror()) {
				unlock(&f->lock_member);
				closefgrp(nf);
				nexterror();
			}
			fgrowfd(nf, i);
			poperror();
		}
		nf->fd[i] = f->fd[i];
		nf->fd[i]->ref_member.ref++;
		if(i > nf->maxfd)
			nf->maxfd = i;
	}
	unlock(&f->lock_member);
	return nf;
}

/* fdinstall: install Chan c at a specific file descriptor slot. */
void
fdinstall(int fd, Chan *c)
{
	Fgrp *f;

	if(up == nil || up->fgrp == nil)
		error("no fgrp");
	f = up->fgrp;

	if(fd < 0 || fd >= FGMAX)
		error("bad fd");

	lock(&f->lock_member);
	if(fd >= f->nfd) {
		if(waserror()) {
			unlock(&f->lock_member);
			nexterror();
		}
		fgrowfd(f, fd);
		poperror();
	}
	if(f->fd[fd] != nil) {
		Chan *oc = f->fd[fd];
		f->fd[fd] = c;
		unlock(&f->lock_member);
		cclose(oc);
	} else {
		f->fd[fd] = c;
		if(fd > f->maxfd)
			f->maxfd = fd;
		unlock(&f->lock_member);
	}
}

