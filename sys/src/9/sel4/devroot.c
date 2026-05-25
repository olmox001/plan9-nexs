/*
 * devroot.c — Root filesystem device (#/) for Plan 9 on seL4.
 *
 * Proxies VFS calls to 9pserver via 9P2000 over the shared ring buffer
 * (devmntsel4 transport).  One persistent session (Tversion + Tattach)
 * is established on first rootattach.  Each Chan gets its own fid so
 * concurrent file opens work correctly in a cooperative scheduler.
 *
 * Fid lifecycle:
 *   rootattach → Twalk(ROOT_FID, clone, nwname=0)   → c->aux = clone
 *   rootwalk   → Twalk(c->aux, walked, names)       → clunk old, c->aux = walked
 *   rootopen   → Topen(c->aux)
 *   rootread   → Tread(c->aux, ...)
 *   rootclose  → Tclunk(c->aux)
 */

#include <u.h>
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "../port/error.h"

extern Dev mntsel4devtab;

/* ── Persistent 9P session state ─────────────────────────────────────────── */

static int    fs_inited;
static Chan   fs_mc;           /* transport Chan (Q9p in mntsel4) */
static u16int fs_next_tag = 1;
static u32int fs_next_fid = 1; /* fid 0 = ROOT_FID (from Tattach), never freed */

#define ROOT_FID  0u

static u32int
fs_alloc_fid(void)
{
	return fs_next_fid++;
}

static u16int
fs_alloc_tag(void)
{
	return fs_next_tag++;
}

/* ── I/O buffers: module-level to keep stack frames small ────────────────── */

static uchar fs_tx[512];
static uchar fs_rx[8192];

/*
 * fs_rpc: marshal req → send → receive → unmarshal rep.
 * Calls error() on transport failure or Rerror from server.
 */
static void
fs_rpc(Fcall *req, Fcall *rep)
{
	int nw, nr;

	nw = convS2M(req, fs_tx, sizeof fs_tx);
	if(nw <= 0)
		error("fs_rpc: convS2M failed");

	mntsel4devtab.write(&fs_mc, fs_tx, nw, 0);

	nr = mntsel4devtab.read(&fs_mc, fs_rx, sizeof fs_rx, 0);
	if(nr <= 0)
		error("fs_rpc: no response");
	if(convM2S(fs_rx, nr, rep) <= 0)
		error("fs_rpc: bad response");
	if(rep->type == Rerror)
		error(rep->ename);
}

/* fs_clunk: release a fid; errors suppressed (close must not fail). */
static void
fs_clunk(u32int fid)
{
	Fcall req, rep;

	if(!fs_inited || fid == ROOT_FID)
		return;
	if(waserror()) {
		return;
	}
	memset(&req, 0, sizeof req);
	req.type = Tclunk;
	req.tag  = fs_alloc_tag();
	req.fid  = fid;
	fs_rpc(&req, &rep);
	poperror();
}

/*
 * fs_init: establish 9P session via Tversion + Tattach.
 * Idempotent: returns immediately if already done.
 * Must be called with up != nil (i.e., from a running proc).
 */
static void
fs_init(void)
{
	Fcall req, rep;

	if(fs_inited)
		return;

	/* Point fs_mc at the Q9p transport file without going through attach/walk.
	 * mntsel4read/write only check c->qid.path == Q9p (=1), nothing else. */
	memset(&fs_mc, 0, sizeof fs_mc);
	fs_mc.qid.path = 1;   /* Q9p */
	fs_mc.qid.type = QTFILE;

	/* Tversion */
	memset(&req, 0, sizeof req);
	req.type    = Tversion;
	req.tag     = NOTAG;
	req.msize   = 8192;
	req.version = "9P2000";
	fs_rpc(&req, &rep);

	/* Tattach: ROOT_FID becomes the permanent root handle */
	memset(&req, 0, sizeof req);
	req.type  = Tattach;
	req.tag   = fs_alloc_tag();
	req.fid   = ROOT_FID;
	req.afid  = NOFID;
	req.uname = "root";
	req.aname = "";
	fs_rpc(&req, &rep);

	fs_inited = 1;
	putstrn("devroot: 9P session up\n", 23);
}

/* ── Device operations ────────────────────────────────────────────────────── */

static void
rootreset(void)
{
}

/*
 * rootattach: open the root namespace.
 * Establishes the 9P session once, then clones ROOT_FID so each
 * caller gets its own fid to walk/open/close independently.
 */
static Chan*
rootattach(char *spec)
{
	Fcall   req, rep;
	u32int  clone;
	Chan   *c;

	fs_init();

	/* Clone root fid (Twalk with nwname=0 per 9P spec) */
	clone = fs_alloc_fid();
	memset(&req, 0, sizeof req);
	req.type   = Twalk;
	req.tag    = fs_alloc_tag();
	req.fid    = ROOT_FID;
	req.newfid = clone;
	req.nwname = 0;
	fs_rpc(&req, &rep);

	c = devattach('/', spec);
	c->aux = (void*)(uintptr)clone;
	return c;
}

/*
 * rootwalk: walk names from c's current fid.
 * On a full walk (nc == nil path from exec.c namec): c->aux is updated
 * to the walked fid and the old clone is clunked.
 */
static Walkqid*
rootwalk(Chan *c, Chan *nc, char **name, int nname)
{
	static struct {
		Walkqid wq;
		Qid     extra[MAXWELEM - 1];
	} pool;
	static Chan clonechan;

	Fcall  req, rep;
	u32int oldfid, newfid;
	Chan  *dst;
	int    i;

	memset(&pool, 0, sizeof pool);

	oldfid = (u32int)(uintptr)c->aux;
	newfid = fs_alloc_fid();

	memset(&req, 0, sizeof req);
	req.type   = Twalk;
	req.tag    = fs_alloc_tag();
	req.fid    = oldfid;
	req.newfid = newfid;
	req.nwname = nname;
	for(i = 0; i < nname && i < MAXWELEM; i++)
		req.wname[i] = name[i];

	fs_rpc(&req, &rep);  /* errors propagate to caller's waserror */


	Qid *wqid = pool.wq.qid;
	for(i = 0; i < rep.nwqid; i++) {
		wqid[i] = rep.wqid[i];
		pool.wq.nqid++;
	}

	if(rep.nwqid == nname) {
		/* Full walk: oldfid (the pre-walk clone) is replaced by newfid */
		if(oldfid != ROOT_FID)
			fs_clunk(oldfid);
		if(nc != nil) {
			dst = nc;
		} else {
			clonechan = *c;
			dst = &clonechan;
			c->aux = (void*)(uintptr)newfid;
		}
		dst->aux = (void*)(uintptr)newfid;
		dst->qid = rep.wqid[rep.nwqid - 1];
		pool.wq.clone = dst;
	} else {
		/* Partial walk: return what we got; newfid is unusable, clunk it */
		fs_clunk(newfid);
		pool.wq.clone = nil;
	}


	return &pool.wq;
}

static int
rootstat(Chan *c, uchar *db, int n)
{
	Fcall  req, rep;
	u32int fid;

	fid = (u32int)(uintptr)c->aux;
	memset(&req, 0, sizeof req);
	req.type = Tstat;
	req.tag  = fs_alloc_tag();
	req.fid  = fid;
	fs_rpc(&req, &rep);

	if(rep.nstat <= 0 || rep.nstat > n)
		return -1;
	memmove(db, rep.stat, rep.nstat);
	return rep.nstat;
}

static Chan*
rootopen(Chan *c, int omode)
{
	Fcall  req, rep;
	u32int fid;

	fid = (u32int)(uintptr)c->aux;
	memset(&req, 0, sizeof req);
	req.type = Topen;
	req.tag  = fs_alloc_tag();
	req.fid  = fid;
	req.mode = omode & 3;
	fs_rpc(&req, &rep);

	c->qid    = rep.qid;
	c->offset = 0;
	c->flag  |= COPEN;
	return c;
}

static void
rootclose(Chan *c)
{
	u32int fid;

	fid = (u32int)(uintptr)c->aux;
	fs_clunk(fid);
	c->aux = (void*)(uintptr)ROOT_FID;
}

static long
rootread(Chan *c, void *buf, long n, vlong off)
{
	Fcall  req, rep;
	u32int fid;
	long   tot, chunk;

	if(c->qid.type & QTDIR)
		return 0;

	fid = (u32int)(uintptr)c->aux;
	tot = 0;
	while(tot < n) {
		chunk = n - tot;
		if(chunk > 4096)
			chunk = 4096;

		memset(&req, 0, sizeof req);
		req.type   = Tread;
		req.tag    = fs_alloc_tag();
		req.fid    = fid;
		req.offset = (uvlong)(off + tot);
		req.count  = (uint)chunk;

		if(waserror()) {
			if(tot > 0) {
				poperror();
				break;
			}
			nexterror();
		}
		fs_rpc(&req, &rep);
		poperror();

		if(rep.count == 0)
			break;
		if(rep.count > (uint)chunk)
			rep.count = (uint)chunk;
		memmove((char*)buf + tot, rep.data, rep.count);
		tot += rep.count;
		if(rep.count < (uint)chunk)
			break;
	}
	return tot;
}

static long
rootwrite(Chan *c, void *buf, long n, vlong off)
{
	USED(c, buf, n, off);
	error(Eperm);
	return 0;
}

/* ── Device table ─────────────────────────────────────────────────────────── */

Dev rootdevtab = {
	'/',
	"root",

	rootreset,
	devinit,
	devshutdown,
	rootattach,
	rootwalk,
	rootstat,
	rootopen,
	devcreate,
	rootclose,
	rootread,
	devbread,
	rootwrite,
	devbwrite,
	devremove,
	devwstat,
};
