/*
 * devpipe.c — Pipe device (#|) for Plan 9 on seL4 Microkit.
 *
 * Cooperative blocking via sleep()/wakeup() instead of spin loops.
 * Each pipe has two Rendez: one for "data available to read", one for
 * "space available to write". sleep() yields to the scheduler; wakeup()
 * makes the sleeping proc ready again.
 */

#include <u.h>
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "../port/error.h"

enum {
	Qdir   = 0,
	Qdata0 = 1,
	Qdata1 = 2,

	Pipeqsize = 32768,
	Maxpipes  = 32,
};

typedef struct Pipe Pipe;
struct Pipe {
	uchar  *buf;
	uint    r;
	uint    w;
	uint    size;
	int     inuse;
	int     refcnt;
	Rendez  rdr;    /* reader sleeps here waiting for data */
	Rendez  wrr;    /* writer sleeps here waiting for space */
};

static Pipe pipes[Maxpipes];

static Dirtab pipedir[] = {
	".",     {Qdir,   0, QTDIR},  0,  DMDIR|0555,
	"data",  {Qdata0, 0, QTFILE}, 0,  0600,
	"data1", {Qdata1, 0, QTFILE}, 0,  0600,
};

static void
pipereset(void)
{
	memset(pipes, 0, sizeof(pipes));
}

static void
pipeinit(void)
{
}

static uvlong
pipepath(int idx, int end)
{
	return ((uvlong)(idx + 1) << 4) | end;
}

static int
pipeidx(uvlong path)
{
	return (int)((path >> 4) & 0xffff) - 1;
}

static int
pipeend(uvlong path)
{
	return (int)(path & 0xf);
}

static Pipe*
pipealloc(void)
{
	int i;

	for(i = 0; i < Maxpipes; i++) {
		if(!pipes[i].inuse) {
			pipes[i].buf = (uchar*)xalloc(Pipeqsize);
			if(pipes[i].buf == nil)
				error("pipe: out of memory");
			pipes[i].r      = 0;
			pipes[i].w      = 0;
			pipes[i].size   = Pipeqsize;
			pipes[i].refcnt = 2;
			pipes[i].inuse  = 1;
			memset(&pipes[i].rdr, 0, sizeof(Rendez));
			memset(&pipes[i].wrr, 0, sizeof(Rendez));
			return &pipes[i];
		}
	}
	return nil;
}

static void
pipefree(Pipe *p)
{
	if(p->buf) {
		xfree(p->buf);
		p->buf = nil;
	}
	memset(p, 0, sizeof(Pipe));
}

/* Condition function: data available in ring buffer */
static int
pipehasdata(void *arg)
{
	Pipe *p = (Pipe*)arg;
	return p->w != p->r;
}

/* Condition function: space available in ring buffer */
static int
pipehasspace(void *arg)
{
	Pipe *p = (Pipe*)arg;
	return (p->size - (p->w - p->r)) > 0;
}

static Chan*
pipeattach(char *spec)
{
	return devattach('|', spec);
}

static Walkqid*
pipewalk(Chan *c, Chan *nc, char **name, int nname)
{
	return devwalk(c, nc, name, nname, pipedir, nelem(pipedir), devgen);
}

static int
pipestat(Chan *c, uchar *db, int n)
{
	Pipe *p;
	Dir   d;
	int   idx;

	memset(&d, 0, sizeof(d));
	if(c->qid.path == Qdir) {
		devdir(c, c->qid, ".", 0, "root", DMDIR|0555, &d);
		return convD2M(&d, db, n);
	}
	idx = pipeidx(c->qid.path);
	if(idx < 0 || idx >= Maxpipes || !pipes[idx].inuse)
		error(Enonexist);
	p        = &pipes[idx];
	d.name   = (pipeend(c->qid.path) == 0) ? "data" : "data1";
	d.qid    = c->qid;
	d.mode   = 0600;
	d.length = p->w - p->r;
	d.uid    = "root";
	d.gid    = "root";
	d.muid   = "root";
	return convD2M(&d, db, n);
}

static Chan*
pipeopen(Chan *c, int omode)
{
	return devopen(c, omode, pipedir, nelem(pipedir), devgen);
}

static void
pipecreate(Chan *c, char *name, int omode, ulong perm)
{
	Pipe *p;
	int   idx;

	USED(name); USED(omode); USED(perm);
	if(c->qid.path != Qdir)
		error(Eperm);
	p = pipealloc();
	if(p == nil)
		error("pipe: too many open pipes");
	idx          = p - pipes;
	c->qid.path  = pipepath(idx, 0);
	c->qid.type  = QTFILE;
	c->qid.vers  = 0;
	c->flag     |= COPEN;
	c->mode      = ORDWR;
	c->offset    = 0;
	c->aux       = p;
}

static void
pipeclose(Chan *c)
{
	Pipe *p;
	int   idx;

	if(c->qid.path == Qdir || !(c->flag & COPEN))
		return;
	idx = pipeidx(c->qid.path);
	if(idx < 0 || idx >= Maxpipes)
		return;
	p = &pipes[idx];
	if(!p->inuse)
		return;
	p->refcnt--;
	/* Wake any sleeper on the other end before freeing */
	wakeup(&p->rdr);
	wakeup(&p->wrr);
	if(p->refcnt <= 0)
		pipefree(p);
}

static long
piperead(Chan *c, void *buf, long n, vlong off)
{
	Pipe *p;
	uint  avail;
	long  count;
	int   idx;
	uint  i;

	USED(off);
	if(c->qid.type & QTDIR)
		return devdirread(c, buf, n, pipedir, nelem(pipedir), devgen);
	idx = pipeidx(c->qid.path);
	if(idx < 0 || idx >= Maxpipes || !pipes[idx].inuse)
		error(Enonexist);
	p = &pipes[idx];
	/* Block until data is available or pipe is closed */
	sleep(&p->rdr, pipehasdata, p);
	if(p->w == p->r)
		return 0;   /* pipe closed, no data */
	avail = p->w - p->r;
	count = (long)avail > n ? n : (long)avail;
	for(i = 0; i < (uint)count; i++)
		((uchar*)buf)[i] = p->buf[(p->r + i) % p->size];
	coherence();
	p->r += count;
	wakeup(&p->wrr);   /* signal that space is available */
	return count;
}

static long
pipewrite(Chan *c, void *buf, long n, vlong off)
{
	Pipe *p;
	uint  free;
	long  count;
	uint  i;
	int   idx;

	USED(off);
	if(c->qid.path == Qdir)
		error(Eperm);
	idx = pipeidx(c->qid.path);
	if(idx < 0 || idx >= Maxpipes || !pipes[idx].inuse)
		error(Enonexist);
	p = &pipes[idx];
	/* Block until there is space */
	sleep(&p->wrr, pipehasspace, p);
	if(!p->inuse)
		error("write to closed pipe");
	free  = p->size - (p->w - p->r);
	count = (long)free > n ? n : (long)free;
	for(i = 0; i < (uint)count; i++)
		p->buf[(p->w + i) % p->size] = ((uchar*)buf)[i];
	coherence();
	p->w += count;
	wakeup(&p->rdr);   /* signal that data is available */
	return count;
}

Dev pipedevtab = {
	'|',
	"pipe",

	pipereset,
	pipeinit,
	devshutdown,
	pipeattach,
	pipewalk,
	pipestat,
	pipeopen,
	pipecreate,
	pipeclose,
	piperead,
	devbread,
	pipewrite,
	devbwrite,
	devremove,
	devwstat,
};
