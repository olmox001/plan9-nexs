/*
 * devsrv.c — Service-registry device (#s) for Plan 9 on seL4 Microkit.
 *
 * /srv is the name-space rendezvous point where servers post file descriptors
 * that clients can then open and use as mount targets.
 *
 *   post(name, fd)     → creates /srv/<name> containing the Chan of fd
 *   open("/srv/<name>")→ returns a Chan through which the client can
 *                         do a mount(2) to attach the server's name space
 *
 * Simplified implementation: a fixed table of (name, Chan*) pairs.
 * No ACL, no hierarchical boards, no lease mechanism.
 */

#include <u.h>
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "../port/error.h"

enum {
	MAXSRV   = 64,
	SRVNAMELEN = 128,

	Qdir = 0,
	Qsrv = 1,
};

typedef struct Srventry Srventry;
struct Srventry {
	char  name[SRVNAMELEN];
	Chan *chan;
	int   inuse;
	ulong qpath;   /* unique qid path */
};

static Srventry srvtab[MAXSRV];
static RWLock   srvlk;
static ulong    srvpath = 1;

static void
srvreset(void)
{
	memset(srvtab, 0, sizeof(srvtab));
}

static void
srvinit(void)
{
}

static int
srvgen(Chan *c, char *name, Dirtab *tab, int ntab, int s, Dir *dp)
{
	Srventry *e;
	Qid       q;
	int       n;

	USED(tab); USED(ntab);

	if(s == DEVDOTDOT) {
		devdir(c, (Qid){Qdir, 0, QTDIR}, ".", 0, "root", DMDIR|0555, dp);
		return 1;
	}

	if(name != nil) {
		rlock(&srvlk);
		for(n = 0; n < MAXSRV; n++) {
			e = &srvtab[n];
			if(e->inuse && strcmp(e->name, name) == 0) {
				q.path = e->qpath;
				q.vers = 0;
				q.type = QTFILE;
				devdir(c, q, e->name, 0, "root", 0600, dp);
				runlock(&srvlk);
				return 1;
			}
		}
		runlock(&srvlk);
		return -1;
	}

	/* Enumerate: skip to the s-th occupied slot */
	rlock(&srvlk);
	n = 0;
	int i;
	for(i = 0; i < MAXSRV; i++) {
		e = &srvtab[i];
		if(!e->inuse)
			continue;
		if(n == s) {
			q.path = e->qpath;
			q.vers = 0;
			q.type = QTFILE;
			devdir(c, q, e->name, 0, "root", 0600, dp);
			runlock(&srvlk);
			return 1;
		}
		n++;
	}
	runlock(&srvlk);
	return -1;
}

static Chan*
srvattach(char *spec)
{
	return devattach('s', spec);
}

static Walkqid*
srvwalk(Chan *c, Chan *nc, char **name, int nname)
{
	return devwalk(c, nc, name, nname, nil, 0, srvgen);
}

static int
srvstat(Chan *c, uchar *db, int n)
{
	return devstat(c, db, n, nil, 0, srvgen);
}

static Chan*
srvopen(Chan *c, int omode)
{
	Srventry *e;
	Chan     *rc;
	int       i;

	if(c->qid.type & QTDIR) {
		if(openmode(omode) != OREAD)
			error(Eisdir);
		c->mode   = OREAD;
		c->flag  |= COPEN;
		c->offset = 0;
		return c;
	}

	/* Find the entry by qpath */
	rlock(&srvlk);
	for(i = 0; i < MAXSRV; i++) {
		e = &srvtab[i];
		if(e->inuse && e->qpath == c->qid.path) {
			rc = e->chan;
			rc->ref_member.ref++;
			runlock(&srvlk);
			cclose(c);
			return rc;
		}
	}
	runlock(&srvlk);
	error(Enonexist);
	return nil;
}

static void
srvcreate(Chan *c, char *name, int omode, ulong perm)
{
	Srventry *e;
	int       i;
	Chan     *nc;

	USED(perm);
	if(c->qid.path != Qdir)
		error(Eperm);
	if(openmode(omode) != OWRITE && openmode(omode) != ORDWR)
		error(Eperm);

	/* Allocate a slot */
	wlock(&srvlk);
	e = nil;
	for(i = 0; i < MAXSRV; i++) {
		if(!srvtab[i].inuse) {
			e = &srvtab[i];
			break;
		}
	}
	if(e == nil) {
		wunlock(&srvlk);
		error("srv: table full");
	}

	nc = newchan();
	nc->flag   |= COPEN;
	nc->mode    = ORDWR;
	kstrcpy(e->name, name, SRVNAMELEN);
	e->chan     = nc;
	e->inuse    = 1;
	e->qpath    = srvpath++;
	c->qid.path = e->qpath;
	c->qid.type = QTFILE;
	c->flag    |= COPEN;
	c->mode     = openmode(omode);
	c->offset   = 0;
	c->aux      = nc;
	wunlock(&srvlk);
}

static void
srvclose(Chan *c)
{
	USED(c);
}

static long
srvread(Chan *c, void *buf, long n, vlong off)
{
	if(c->qid.type & QTDIR)
		return devdirread(c, buf, n, nil, 0, srvgen);
	USED(off);
	error(Eperm);
	return 0;
}

static long
srvwrite(Chan *c, void *buf, long n, vlong off)
{
	Srventry *e;
	Chan     *src;
	int       fd, i;
	char      tmp[16];

	USED(off);
	if(c->qid.type & QTDIR)
		error(Eperm);

	/* Write an fd number to post a channel into this srv slot */
	if(n >= (long)sizeof(tmp))
		n = sizeof(tmp) - 1;
	memmove(tmp, buf, n);
	tmp[n] = '\0';
	fd = (int)strtoul(tmp, nil, 10);
	src = fdtochan(fd, ORDWR, 0, 1);

	wlock(&srvlk);
	for(i = 0; i < MAXSRV; i++) {
		e = &srvtab[i];
		if(e->inuse && e->qpath == c->qid.path) {
			if(e->chan != nil)
				cclose(e->chan);
			e->chan = src;
			wunlock(&srvlk);
			return n;
		}
	}
	wunlock(&srvlk);
	cclose(src);
	error("srv: entry not found");
	return 0;
}

static void
srvremove(Chan *c)
{
	Srventry *e;
	int       i;

	wlock(&srvlk);
	for(i = 0; i < MAXSRV; i++) {
		e = &srvtab[i];
		if(e->inuse && e->qpath == c->qid.path) {
			if(e->chan != nil) {
				cclose(e->chan);
				e->chan = nil;
			}
			memset(e, 0, sizeof(Srventry));
			wunlock(&srvlk);
			return;
		}
	}
	wunlock(&srvlk);
	error(Enonexist);
}

Dev srvdevtab = {
	's',
	"srv",

	srvreset,
	srvinit,
	devshutdown,
	srvattach,
	srvwalk,
	srvstat,
	srvopen,
	srvcreate,
	srvclose,
	srvread,
	devbread,
	srvwrite,
	devbwrite,
	srvremove,
	devwstat,
};
