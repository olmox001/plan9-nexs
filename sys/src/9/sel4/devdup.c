/*
 * devdup.c — Dup device (#d) for Plan 9 on seL4 Microkit.
 *
 * The dup device maps the process's file-descriptor table into the namespace.
 * Walking /fd/<n> opens the same channel as fd n.  Walking /fd/<n>ctl reads
 * metadata about that channel.
 *
 * Qid encoding: path = 2*fd + (1 if ctl file, 0 if data file) + 1
 *   path 0 → directory
 *   path 1 → fd 0 data file
 *   path 2 → fd 0 ctl file
 *   path 3 → fd 1 data file
 *   ...
 */

#include <u.h>
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "../port/error.h"

static int
dupgen(Chan *c, char *name, Dirtab *tab, int ntab, int s, Dir *dp)
{
	Fgrp *fgrp;
	Chan *f;
	Qid   q;
	int   fd, p;
	static int perm[] = { 0400, 0200, 0600, 0 };

	USED(tab); USED(ntab);
	fgrp = up->fgrp;

	if(s == DEVDOTDOT) {
		devdir(c, c->qid, ".", 0, eve, 0555, dp);
		return 1;
	}
	if(s == 0)
		return 0;
	s--;
	if(fgrp == nil || s / 2 > fgrp->maxfd)
		return -1;
	f = fgrp->fd[s / 2];
	if(f == nil)
		return 0;

	fd = s / 2;
	if(name != nil) {
		/* Name lookup: parse fd number from name */
		char *ep;
		fd = (int)strtoul(name, &ep, 10);
		if(*ep == '\0')
			s = fd * 2;
		else if(*ep == 'c' && *(ep + 1) == 't' && *(ep + 2) == 'l' && *(ep + 3) == '\0')
			s = fd * 2 + 1;
		else
			return -1;
		if(fd < 0 || fgrp == nil || fd > fgrp->maxfd)
			return -1;
		f = fgrp->fd[fd];
		if(f == nil)
			return -1;
	}

	if(s & 1) {
		p = 0400;
		sprint(up->genbuf, "%dctl", s / 2);
	} else {
		p = perm[f->mode & 3];
		sprint(up->genbuf, "%d", s / 2);
	}
	mkqid(&q, s + 1, 0, QTFILE);
	devdir(c, q, up->genbuf, 0, eve, p, dp);
	return 1;
}

static Chan*
dupattach(char *spec)
{
	return devattach('d', spec);
}

static Walkqid*
dupwalk(Chan *c, Chan *nc, char **name, int nname)
{
	return devwalk(c, nc, name, nname, nil, 0, dupgen);
}

static int
dupstat(Chan *c, uchar *db, int n)
{
	return devstat(c, db, n, nil, 0, dupgen);
}

static Chan*
dupopen(Chan *c, int omode)
{
	Chan *f;
	int   fd, twicefd;

	if(omode & ORCLOSE)
		error(Eperm);
	if(c->qid.type & QTDIR) {
		if(omode != 0)
			error(Eisdir);
		c->mode   = 0;
		c->flag  |= COPEN;
		c->offset = 0;
		return c;
	}
	if(c->qid.type & QTAUTH)
		error(Eperm);

	twicefd = (int)c->qid.path - 1;
	fd      = twicefd / 2;

	if(twicefd & 1) {
		/* ctl file — open in place */
		f         = c;
		f->mode   = openmode(omode);
		f->flag  |= COPEN;
		f->offset = 0;
	} else {
		/* data file — return the actual channel */
		f = fdtochan(fd, openmode(omode), 0, 1);
		cclose(c);
	}
	return f;
}

static void
dupclose(Chan *c)
{
	USED(c);
}

static long
dupread(Chan *c, void *va, long n, vlong offset)
{
	char *a = (char*)va;
	char  buf[256];
	int   fd, twicefd;

	if(c->qid.type == QTDIR)
		return devdirread(c, a, n, nil, 0, dupgen);
	twicefd = (int)c->qid.path - 1;
	fd      = twicefd / 2;
	if(twicefd & 1) {
		/* ctl file — read channel metadata */
		Chan *f;
		f = fdtochan(fd, -1, 0, 1);
		if(waserror()) {
			cclose(f);
			nexterror();
		}
		snprint(buf, sizeof buf, "fd %d type %c path %llud\n",
			fd, devtab[f->type] ? devtab[f->type]->dc : '?',
			(uvlong)f->qid.path);
		cclose(f);
		poperror();
		return readstr((ulong)offset, va, n, buf);
	}
	panic("dupread: not a ctl file");
	return 0;
}

static long
dupwrite(Chan *c, void *va, long n, vlong offset)
{
	USED(c, va, n, offset);
	error(Eperm);
	return 0;
}

Dev dupdevtab = {
	'd',
	"dup",

	devreset,
	devinit,
	devshutdown,
	dupattach,
	dupwalk,
	dupstat,
	dupopen,
	devcreate,
	dupclose,
	dupread,
	devbread,
	dupwrite,
	devbwrite,
	devremove,
	devwstat,
};
