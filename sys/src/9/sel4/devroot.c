/*
 * devroot.c — Self-contained root VFS device for Plan 9 on seL4.
 *
 * This device implements the '/' namespace with a /sel4 subdirectory
 * and a virtual /sel4/hello.txt file backed by the 9P seL4 transport.
 *
 * Design constraints:
 *   - rootwalk is heap-free: uses a static oversized Walkqid pool.
 *   - No calls to devwalk, devclone, isdir or newchan.
 *   - No dependency on devtab[c->type] (which requires a registered device).
 *
 * All of the above are impossible to use during early boot on seL4 Microkit
 * because they require xinit(), a registered devtab, and a complete Proc.
 */

#include <u.h>
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "../port/error.h"

enum {
	Qroot  = 0,
	Qsel4  = 1,
	Qhello = 2,
};

/*
 * Static Walkqid pool — holds up to 8 Qids, more than enough for our
 * flat two-level namespace. Safe because seL4 Microkit is single-threaded
 * within a Protection Domain (cooperative scheduling).
 */
typedef struct {
	Walkqid wq;
	Qid     extra[7]; /* brings total embedded Qid count to 8 */
} WalkqidPool;

/* ── rootwalk: fully self-contained, heap-free path walk ─────────────────── */
static Walkqid*
rootwalk(Chan *c, Chan *nc, char **name, int nname)
{
	static WalkqidPool pool;
	static Chan clonechan;

	memset(&pool, 0, sizeof(pool));

	/*
	 * Work on a clone of c so we can mutate qid as we traverse.
	 * nc is ignored (we don't do heap allocation via devclone).
	 */
	USED(nc);
	clonechan = *c;
	Chan *cur = &clonechan;

	int i;
	for(i = 0; i < nname; i++) {
		char *n = name[i];

		/* "." — stays at current node */
		if(strcmp(n, ".") == 0) {
			pool.wq.qid[pool.wq.nqid++] = cur->qid;
			continue;
		}

		/* ".." — always resolves to root */
		if(strcmp(n, "..") == 0) {
			cur->qid.path = Qroot;
			cur->qid.type = QTDIR;
			cur->qid.vers = 0;
			pool.wq.qid[pool.wq.nqid++] = cur->qid;
			continue;
		}

		switch((int)cur->qid.path) {
		case Qroot:
			if(strcmp(n, "sel4") == 0) {
				cur->qid.path = Qsel4;
				cur->qid.type = QTDIR;
				cur->qid.vers = 0;
				pool.wq.qid[pool.wq.nqid++] = cur->qid;
				continue;
			}
			goto notfound;

		case Qsel4:
			if(strcmp(n, "hello.txt") == 0) {
				cur->qid.path = Qhello;
				cur->qid.type = QTFILE;
				cur->qid.vers = 0;
				pool.wq.qid[pool.wq.nqid++] = cur->qid;
				continue;
			}
			goto notfound;

		default:
			/* Trying to walk into a file */
			goto notfound;
		}

	notfound:
		if(i == 0)
			return nil; /* first element not found: fail entirely */
		/* Partial walk: stop here */
		goto done;
	}

done:
	if(pool.wq.nqid == nname && nname > 0) {
		/* Full walk succeeded */
		pool.wq.clone = cur;
	} else if(pool.wq.nqid == 0) {
		return nil;
	} else {
		/* Partial walk: caller sees nqid < nname and clone == nil */
		pool.wq.clone = nil;
	}
	return &pool.wq;
}

/* ── rootreset / rootattach ───────────────────────────────────────────────── */
static void
rootreset(void)
{
}

static Chan*
rootattach(char *spec)
{
	USED(spec);
	return nil; /* not used in standalone early-boot test */
}

/* ── rootstat ─────────────────────────────────────────────────────────────── */
static int
rootstat(Chan *c, uchar *db, int n)
{
	Dir d;
	memset(&d, 0, sizeof(d));

	switch((int)c->qid.path) {
	case Qroot:
		d.name = ".";
		d.qid  = c->qid;
		d.mode = DMDIR | 0555;
		d.uid  = "root";
		d.gid  = "root";
		d.muid = "root";
		break;
	case Qsel4:
		d.name = "sel4";
		d.qid  = c->qid;
		d.mode = DMDIR | 0555;
		d.uid  = "root";
		d.gid  = "root";
		d.muid = "root";
		break;
	case Qhello:
		d.name   = "hello.txt";
		d.qid    = c->qid;
		d.mode   = 0444;
		d.length = 27;
		d.uid    = "root";
		d.gid    = "root";
		d.muid   = "root";
		break;
	default:
		return -1;
	}
	return convD2M(&d, db, n);
}

/* ── rootopen ─────────────────────────────────────────────────────────────── */
static Chan*
rootopen(Chan *c, int omode)
{
	switch((int)c->qid.path) {
	case Qroot:
	case Qsel4:
		if(omode != OREAD && omode != OEXEC)
			error(Eperm);
		break;
	case Qhello:
		c->offset = 0;
		c->mode   = openmode(omode);
		c->flag  |= COPEN;
		break;
	default:
		error(Enonexist);
	}
	return c;
}

/* ── rootclose ────────────────────────────────────────────────────────────── */
static void
rootclose(Chan *c)
{
	USED(c);
}

/* ── rootread ─────────────────────────────────────────────────────────────── */
static long
rootread(Chan *c, void *buf, long n, vlong off)
{
	if(c->qid.type & QTDIR)
		return 0; /* directory reads not implemented in this stub */

	if((int)c->qid.path == Qhello) {
		extern Dev mntsel4devtab;
		Chan mc;
		uchar tx[256], rx[256];
		Fcall req, rep;
		int nw;

		memset(&mc, 0, sizeof(mc));
		mc.qid.path = 1; /* Q9p */
		mc.qid.type = QTFILE;

		/* 1. Tversion */
		memset(&req, 0, sizeof(req));
		req.type    = Tversion;
		req.tag     = NOTAG;
		req.msize   = 8192;
		req.version = "9P2000";
		nw = (int)convS2M(&req, tx, sizeof(tx));
		if(nw <= 0) return -1;
		mntsel4devtab.write(&mc, tx, nw, 0);
		nw = mntsel4devtab.read(&mc, rx, sizeof(rx), 0);
		if(nw <= 0) return -1;
		if((int)convM2S(rx, nw, &rep) != nw) return -1;

		/* 2. Tattach */
		memset(&req, 0, sizeof(req));
		req.type  = Tattach;
		req.tag   = 0;
		req.fid   = 0;
		req.afid  = NOFID;
		req.uname = "root";
		req.aname = "";
		nw = (int)convS2M(&req, tx, sizeof(tx));
		if(nw <= 0) return -1;
		mntsel4devtab.write(&mc, tx, nw, 0);
		nw = mntsel4devtab.read(&mc, rx, sizeof(rx), 0);
		if(nw <= 0) return -1;
		if((int)convM2S(rx, nw, &rep) != nw) return -1;

		/* 3. Twalk */
		memset(&req, 0, sizeof(req));
		req.type     = Twalk;
		req.tag      = 1;
		req.fid      = 0;
		req.newfid   = 1;
		req.nwname   = 1;
		req.wname[0] = "hello.txt";
		nw = (int)convS2M(&req, tx, sizeof(tx));
		if(nw <= 0) return -1;
		mntsel4devtab.write(&mc, tx, nw, 0);
		nw = mntsel4devtab.read(&mc, rx, sizeof(rx), 0);
		if(nw <= 0) return -1;
		if((int)convM2S(rx, nw, &rep) != nw) return -1;

		/* 4. Topen */
		memset(&req, 0, sizeof(req));
		req.type = Topen;
		req.tag  = 2;
		req.fid  = 1;
		req.mode = OREAD;
		nw = (int)convS2M(&req, tx, sizeof(tx));
		if(nw <= 0) return -1;
		mntsel4devtab.write(&mc, tx, nw, 0);
		nw = mntsel4devtab.read(&mc, rx, sizeof(rx), 0);
		if(nw <= 0) return -1;
		if((int)convM2S(rx, nw, &rep) != nw) return -1;

		/* 5. Tread */
		memset(&req, 0, sizeof(req));
		req.type   = Tread;
		req.tag    = 3;
		req.fid    = 1;
		req.offset = off;
		req.count  = n;
		nw = (int)convS2M(&req, tx, sizeof(tx));
		if(nw <= 0) return -1;
		mntsel4devtab.write(&mc, tx, nw, 0);
		nw = mntsel4devtab.read(&mc, rx, sizeof(rx), 0);
		if(nw <= 0) return -1;
		if((int)convM2S(rx, nw, &rep) != nw) return -1;

		if(rep.type == Rread) {
			if(rep.count > (uint)n)
				rep.count = n;
			memmove(buf, rep.data, rep.count);
			return rep.count;
		}
		return -1;
	}
	return 0;
}

/* ── rootwrite ────────────────────────────────────────────────────────────── */
static long
rootwrite(Chan *c, void *buf, long n, vlong off)
{
	USED(c, buf, n, off);
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
