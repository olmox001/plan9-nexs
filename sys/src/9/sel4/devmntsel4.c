/*
 * devmntsel4.c — 9P transport device (#m) for Plan 9 on seL4 Microkit.
 *
 * Bridges plan9_root ↔ 9pserver via the shared ring buffer at SHARE9P_BASE.
 * T-messages are written to tx ring; R-messages arrive in rx ring.
 *
 * The 9pserver sends a seL4 notification on channel SHARE9P_CH when an
 * R-message is ready. notified() calls mnt9p_wakeup() which unblocks the
 * sleeping reader via a Rendez. Without this, mntsel4read would spin forever
 * in a cooperative single-CPU PD, starving all other processes.
 */

#include <u.h>
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "../port/error.h"

enum {
	Qdir = 0,
	Q9p  = 1,
};

static Dirtab mntsel4dir[] = {
	".",  {Qdir, 0, QTDIR},  0,  DMDIR|0555,
	"9p", {Q9p,  0, QTFILE}, 0,  0666,
};

static Rendez mnt9p_rdz;   /* woken by mnt9p_wakeup() from notified() */
static Rendez mnt9p_txrdz; /* woken when tx space becomes available */

static int
mnt9p_hasrx(void *arg)
{
	Shared9P *s = (Shared9P*)SHARE9P_BASE;
	USED(arg);
	coherence();
	return s->rx.w != s->rx.r;
}

static int
mnt9p_hastxspace(void *arg)
{
	Shared9P *s = (Shared9P*)SHARE9P_BASE;
	uint need = (uint)(uintptr)arg;
	coherence();
	return sizeof(s->tx.data) - (s->tx.w - s->tx.r) >= need;
}

/* Called from main.c notified() when 9pserver posts an R-message */
void
mnt9p_wakeup(void)
{
	wakeup(&mnt9p_rdz);
}

static void
mntsel4reset(void)
{
}

static void
mntsel4init(void)
{
}

static Chan*
mntsel4attach(char *spec)
{
	return devattach('m', spec);
}

static Walkqid*
mntsel4walk(Chan *c, Chan *nc, char **name, int nname)
{
	return devwalk(c, nc, name, nname, mntsel4dir, nelem(mntsel4dir), devgen);
}

static int
mntsel4stat(Chan *c, uchar *db, int n)
{
	return devstat(c, db, n, mntsel4dir, nelem(mntsel4dir), devgen);
}

static Chan*
mntsel4open(Chan *c, int omode)
{
	return devopen(c, omode, mntsel4dir, nelem(mntsel4dir), devgen);
}

static void
mntsel4close(Chan *c)
{
	USED(c);
}

static long
mntsel4read(Chan *c, void *buf, long n, vlong off)
{
	Shared9P *s = (Shared9P*)SHARE9P_BASE;
	uint w, r, avail;

	USED(off);

	if(c->qid.type & QTDIR)
		return devdirread(c, buf, n, mntsel4dir, nelem(mntsel4dir), devgen);

	if(c->qid.path != Q9p)
		error(Egreg);

	/* Block until 9pserver delivers an R-message */
	sleep(&mnt9p_rdz, mnt9p_hasrx, nil);

	w     = s->rx.w;
	r     = s->rx.r;
	avail = w - r;
	if((uint)n > avail)
		n = (long)avail;

	for(long i = 0; i < n; i++)
		((uchar*)buf)[i] = s->rx.data[(r + (uint)i) % sizeof(s->rx.data)];
	coherence();
	s->rx.r = r + (uint)n;

	/* If more data remains, keep the rdz hot so next read doesn't miss it */
	if(s->rx.w != s->rx.r)
		wakeup(&mnt9p_rdz);

	return n;
}

static long
mntsel4write(Chan *c, void *buf, long n, vlong off)
{
	Shared9P *s = (Shared9P*)SHARE9P_BASE;
	uint w;

	USED(off);

	if(c->qid.path != Q9p)
		error(Egreg);

	/* Yield until space is available; 9pserver normally drains quickly */
	sleep(&mnt9p_txrdz, mnt9p_hastxspace, (void*)(uintptr)(uint)n);

	w = s->tx.w;
	for(long i = 0; i < n; i++)
		s->tx.data[(w + (uint)i) % sizeof(s->tx.data)] = ((uchar*)buf)[i];
	coherence();
	s->tx.w = w + (uint)n;

	plan9_microkit_notify(SHARE9P_CH);

	/* Wake any other writer that may be waiting for space */
	wakeup(&mnt9p_txrdz);

	return n;
}

Dev mntsel4devtab = {
	'm',
	"mntsel4",

	mntsel4reset,
	mntsel4init,
	devshutdown,
	mntsel4attach,
	mntsel4walk,
	mntsel4stat,
	mntsel4open,
	devcreate,
	mntsel4close,
	mntsel4read,
	devbread,
	mntsel4write,
	devbwrite,
	devremove,
	devwstat,
};
