/*
 * 9pserver.c — Plan 9 rootfs file server for seL4 Microkit.
 *
 * Serves a hierarchical file system via the 9P2000 protocol over the shared
 * ring buffer at SHARE9P_BASE.  The file tree is built from a static list of
 * entries (for simplicity); real binaries are embedded as byte arrays
 * compiled in from the host filesystem.
 *
 * File tree served:
 *   /                 DMDIR 0555
 *   /bin/             DMDIR 0555
 *   /bin/rc           ELF binary (embedded)
 *   /bin/ls           ELF binary (embedded)
 *   /bin/cat          ELF binary (embedded)
 *   /rc/              DMDIR 0555
 *   /rc/bin/          DMDIR 0555
 *   /rc/bin/termrc    rc script
 *   /lib/             DMDIR 0555
 *   /lib/font/        DMDIR 0555
 *   /env/             DMDIR 0555  (mirror of devenv entries)
 *   /dev/             DMDIR 0555  (placeholder — real devs in plan9_root)
 *   /usr/             DMDIR 0555
 *   /usr/root/        DMDIR 0755
 *
 * To embed binaries: run tools/embed_binaries.sh which converts Plan 9 ELF
 * binaries into C arrays included here.  If no binaries are embedded, those
 * files return empty data (the exec() call will fail gracefully).
 *
 * The fid table tracks open files between Tattach/Tclunk.
 */

#include <u.h>
#include <libc.h>
#include <fcall.h>
#define fault  microkit_fault
#define strcpy microkit_strcpy
#include <microkit.h>
#undef fault
#undef strcpy

/* ── Shared ring buffer (same layout as devmntsel4.c) ─────────────────────── */
#define SHARE9P_BASE 0x20000000ULL
#define SHARE9P_CH   1

typedef struct Ring9P Ring9P;
struct Ring9P { volatile uint w; volatile uint r; volatile uchar data[32760]; };

typedef struct Shared9P Shared9P;
struct Shared9P { Ring9P tx; Ring9P rx; };

/* ── UART helper ─────────────────────────────────────────────────────────── */
#define UART_BASE 0x09000000ULL
#define UART_DR   ((volatile u32int*)(UART_BASE))
#define UART_FR   ((volatile u32int*)(UART_BASE + 0x18))
#define FR_TXFF   (1u << 5)

static void
srv_puts(char *s)
{
	while(*s) {
		while(*UART_FR & FR_TXFF) ;
		*UART_DR = (u32int)*s++;
	}
}

/* ── Embedded file content ────────────────────────────────────────────────── */
static uchar rc_script_termrc[] =
	"#!/bin/rc\n"
	"# /rc/bin/termrc — terminal init script\n"
	"path=(/bin /rc/bin)\n"
	"prompt=('% ' '  ')\n"
	"status=''\n";

static uchar rc_script_rcmain[] =
	"# rcmain: Plan 9 version\n"
	"if(~ $#home 0) home=/\n"
	"if(~ $#ifs 0) ifs=' \t\n'\n"
	"switch($#prompt){\n"
	"case 0\n"
	"\tprompt=('% ' '\t')\n"
	"case 1\n"
	"\tprompt=($prompt '\t')\n"
	"}\n"
	"if(~ $rcname ?.out) prompt=('broken! ' '\t')\n"
	"if(flag p) path=/bin\n"
	"if not{\n"
	"\tif(~ $#path 0) path=(/bin .)\n"
	"}\n"
	"fn sigexit\n"
	"if(! ~ $#cflag 0){\n"
	"\tstatus=''\n"
	"\teval $cflag\n"
	"}\n"
	"if not if(flag i){\n"
	"\tstatus=''\n"
	"\tif(! ~ $#* 0) . $*\n"
	"\t. -i '#d/0'\n"
	"}\n"
	"if not if(~ $#* 0) . '#d/0'\n"
	"if not{\n"
	"\tstatus=''\n"
	"\t. $*\n"
	"}\n";

static uchar env_rcname[] = "rc";
static uchar env_pid[]    = "2";
static uchar env_cflag[]  = "";

#ifndef EMBED_BINARIES
static uchar rc_bin_rc[] =
	"#! /bin/rc\n"
	"# rc: minimal shell placeholder\n"
	"# Replace this file with a real Plan 9 rc binary.\n";

/* ── Static file system tree ────────────────────────────────────────────── */
enum { MAXFID = 64, MAXPATH = 256, MAXFILES = 64 };

typedef struct FSEntry FSEntry;
struct FSEntry {
	char   path[MAXPATH];   /* full path, e.g. "/bin/rc" */
	int    isdir;
	ulong  mode;
	uchar *data;            /* nil for directories */
	ulong  size;
};

static FSEntry fstree[] = {
	{ "/",              1, DMDIR|0555, nil, 0 },
	{ "/bin",           1, DMDIR|0555, nil, 0 },
	{ "/bin/rc",        0, 0755, rc_bin_rc,      sizeof(rc_bin_rc)-1 },
	{ "/rc",            1, DMDIR|0555, nil, 0 },
	{ "/rc/bin",        1, DMDIR|0555, nil, 0 },
	{ "/rc/bin/termrc", 0, 0755, rc_script_termrc, sizeof(rc_script_termrc)-1 },
	{ "/rc/lib",        1, DMDIR|0555, nil, 0 },
	{ "/rc/lib/rcmain", 0, 0755, rc_script_rcmain, sizeof(rc_script_rcmain)-1 },
	{ "/lib",           1, DMDIR|0555, nil, 0 },
	{ "/lib/font",      1, DMDIR|0555, nil, 0 },
	{ "/env",           1, DMDIR|0555, nil, 0 },
	{ "/env/rcname",    0, 0666, env_rcname,     sizeof(env_rcname)-1 },
	{ "/env/pid",       0, 0666, env_pid,        sizeof(env_pid)-1 },
	{ "/env/cflag",     0, 0666, env_cflag,      sizeof(env_cflag)-1 },
	{ "/dev",           1, DMDIR|0555, nil, 0 },
	{ "/usr",           1, DMDIR|0555, nil, 0 },
	{ "/usr/root",      1, DMDIR|0755, nil, 0 },
};
#else
#include "embedded_bins.h"

/* ── Static file system tree ────────────────────────────────────────────── */
enum { MAXFID = 64, MAXPATH = 256, MAXFILES = 64 };

typedef struct FSEntry FSEntry;
struct FSEntry {
	char   path[MAXPATH];   /* full path, e.g. "/bin/rc" */
	int    isdir;
	ulong  mode;
	uchar *data;            /* nil for directories */
	ulong  size;
};

static FSEntry fstree[] = {
	{ "/",              1, DMDIR|0555, nil, 0 },
	{ "/bin",           1, DMDIR|0555, nil, 0 },
	{ "/bin/rc",        0, 0755, bin_rc,         sizeof(bin_rc) },
	{ "/bin/echo",      0, 0755, bin_echo,       sizeof(bin_echo) },
	{ "/bin/cat",       0, 0755, bin_cat,        sizeof(bin_cat) },
	{ "/bin/ls",        0, 0755, bin_ls,         sizeof(bin_ls) },
	{ "/bin/pwd",       0, 0755, bin_pwd,        sizeof(bin_pwd) },
	{ "/bin/mkdir",     0, 0755, bin_mkdir,      sizeof(bin_mkdir) },
	{ "/bin/rm",        0, 0755, bin_rm,         sizeof(bin_rm) },
	{ "/rc",            1, DMDIR|0555, nil, 0 },
	{ "/rc/bin",        1, DMDIR|0555, nil, 0 },
	{ "/rc/bin/termrc", 0, 0755, rc_script_termrc, sizeof(rc_script_termrc)-1 },
	{ "/rc/lib",        1, DMDIR|0555, nil, 0 },
	{ "/rc/lib/rcmain", 0, 0755, rc_script_rcmain, sizeof(rc_script_rcmain)-1 },
	{ "/lib",           1, DMDIR|0555, nil, 0 },
	{ "/lib/font",      1, DMDIR|0555, nil, 0 },
	{ "/env",           1, DMDIR|0555, nil, 0 },
	{ "/env/rcname",    0, 0666, env_rcname,     sizeof(env_rcname)-1 },
	{ "/env/pid",       0, 0666, env_pid,        sizeof(env_pid)-1 },
	{ "/env/cflag",     0, 0666, env_cflag,      sizeof(env_cflag)-1 },
	{ "/dev",           1, DMDIR|0555, nil, 0 },
	{ "/usr",           1, DMDIR|0555, nil, 0 },
	{ "/usr/root",      1, DMDIR|0755, nil, 0 },
};
#endif
#define NFSTREE ((int)(sizeof(fstree)/sizeof(fstree[0])))

/* ── FID table ───────────────────────────────────────────────────────────── */
typedef struct Fid Fid;
struct Fid {
	uint    fid;
	int     in_use;
	int     open;    /* OREAD, OWRITE, etc. */
	int     fsidx;   /* index into fstree, or -1 */
};

static Fid fids[MAXFID];

static Fid*
fid_lookup(uint fid)
{
	int i;
	for(i = 0; i < MAXFID; i++)
		if(fids[i].in_use && fids[i].fid == fid)
			return &fids[i];
	return nil;
}

static Fid*
fid_alloc(uint fid)
{
	int i;
	for(i = 0; i < MAXFID; i++) {
		if(!fids[i].in_use) {
			memset(&fids[i], 0, sizeof(Fid));
			fids[i].fid    = fid;
			fids[i].in_use = 1;
			fids[i].fsidx  = 0;   /* root */
			return &fids[i];
		}
	}
	return nil;
}

static void
fid_free(Fid *f)
{
	if(f) f->in_use = 0;
}

/* ── Path lookup in fstree ───────────────────────────────────────────────── */
static int
fs_find(char *path)
{
	int i;
	for(i = 0; i < NFSTREE; i++)
		if(strcmp(fstree[i].path, path) == 0)
			return i;
	return -1;
}

/* Walk one component from parent path, return new index or -1 */
static int
fs_walk(int parent, char *name)
{
	char newpath[MAXPATH];
	FSEntry *p = &fstree[parent];
	int n;

	if(!p->isdir)
		return -1;
	if(strcmp(name, ".") == 0)
		return parent;
	if(strcmp(name, "..") == 0) {
		/* find the parent directory */
		n = strlen(p->path);
		if(n <= 1)
			return 0;   /* already at root */
		memmove(newpath, p->path, n+1);
		char *slash = strrchr(newpath, '/');
		if(slash == newpath) { newpath[1] = '\0'; }
		else                 { *slash = '\0'; }
		return fs_find(newpath);
	}
	/* child */
	if(strcmp(p->path, "/") == 0)
		snprint(newpath, sizeof newpath, "/%s", name);
	else
		snprint(newpath, sizeof newpath, "%s/%s", p->path, name);
	return fs_find(newpath);
}

/* Build Qid for a fstree entry */
static Qid
fs_qid(int idx)
{
	Qid q;
	q.path = (uvlong)(idx + 1);
	q.vers = 0;
	q.type = fstree[idx].isdir ? QTDIR : QTFILE;
	return q;
}

/* Pack Dir for a fstree entry */
static int
fs_dir(int idx, uchar *buf, int max)
{
	FSEntry *e = &fstree[idx];
	Dir      d;

	memset(&d, 0, sizeof d);
	/* name = last component */
	char *slash = strrchr(e->path, '/');
	d.name   = slash ? slash + 1 : e->path;
	if(d.name[0] == '\0') d.name = "/";
	d.qid    = fs_qid(idx);
	d.mode   = e->mode;
	d.length = e->size;
	d.uid    = "root";
	d.gid    = "root";
	d.muid   = "root";
	d.atime  = d.mtime = 0;
	return convD2M(&d, buf, max);
}

/* ── 9P message handling ─────────────────────────────────────────────────── */
static uchar msg_in[8192];
static uchar msg_out[8192];

static void
handle_9p(Shared9P *s)
{
	Fcall req, rep;
	uint  len, avail, w, r;
	int   out_len;

	while(s->tx.w != s->tx.r) {
		w     = s->tx.w;
		r     = s->tx.r;
		avail = w - r;
		if(avail < 4) return;

		/* read message length */
		uint mlen;
		int  i;
		for(i = 0; i < 4; i++)
			((uchar*)&mlen)[i] = s->tx.data[(r + i) % sizeof(s->tx.data)];

		if(avail < mlen) return;
		for(len = 0; len < mlen; len++)
			msg_in[len] = s->tx.data[(r + len) % sizeof(s->tx.data)];
		s->tx.r = r + mlen;

		if((uint)convM2S(msg_in, mlen, &req) != mlen) {
			srv_puts("9pserver: bad message\n");
			continue;
		}

		memset(&rep, 0, sizeof rep);
		rep.tag  = req.tag;
		rep.type = req.type + 1;   /* Rxxx = Txxx + 1 */

		switch(req.type) {

		case Tversion:
			rep.msize   = req.msize < 8192 ? req.msize : 8192;
			rep.version = "9P2000";
			break;

		case Tattach: {
			Fid *f = fid_alloc(req.fid);
			if(f == nil) { rep.type = Rerror; rep.ename = "no fids"; break; }
			f->fsidx = 0;   /* root */
			rep.qid  = fs_qid(0);
			break;
		}

		case Twalk: {
			Fid *f = fid_lookup(req.fid);
			if(f == nil) { rep.type = Rerror; rep.ename = "unknown fid"; break; }

			char dbgbuf[256];
			snprint(dbgbuf, sizeof dbgbuf, "9pserver Twalk fid=%ud newfid=%ud nwname=%d\n", req.fid, req.newfid, req.nwname);
			srv_puts(dbgbuf);

			int cur   = f->fsidx;
			int nok   = 0;
			Qid qids[MAXWELEM];

			for(i = 0; i < req.nwname; i++) {
				int next = fs_walk(cur, req.wname[i]);
				snprint(dbgbuf, sizeof dbgbuf, "  walk step %d: %s from idx %d -> idx %d\n", i, req.wname[i], cur, next);
				srv_puts(dbgbuf);
				if(next < 0) break;
				qids[nok++] = fs_qid(next);
				cur = next;
			}

			snprint(dbgbuf, sizeof dbgbuf, "  walk result: nok=%d\n", nok);
			srv_puts(dbgbuf);

			if(nok < req.nwname) {
				if(nok == 0) { rep.type = Rerror; rep.ename = "file not found"; break; }
			}

			/* Clone into newfid if different */
			if(req.newfid != req.fid) {
				Fid *nf = fid_alloc(req.newfid);
				if(nf == nil) { rep.type = Rerror; rep.ename = "no fids"; break; }
				nf->fsidx = cur;
			} else {
				f->fsidx = cur;
			}

			rep.nwqid = nok;
			memmove(rep.wqid, qids, nok * sizeof(Qid));
			break;
		}

		case Topen: {
			Fid *f = fid_lookup(req.fid);
			if(f == nil) { rep.type = Rerror; rep.ename = "unknown fid"; break; }
			f->open = req.mode;
			rep.qid    = fs_qid(f->fsidx);
			rep.iounit = 0;
			break;
		}

		case Tread: {
			Fid      *f = fid_lookup(req.fid);
			FSEntry  *e;
			uchar     dirbuf[512];
			int       dlen, n;

			if(f == nil) { rep.type = Rerror; rep.ename = "unknown fid"; break; }
			e = &fstree[f->fsidx];

			if(e->isdir) {
				/* Directory read: enumerate children */
				int  pos = 0;
				uint skip = (uint)req.offset;
				uint cnt  = 0;

				for(n = 0; n < NFSTREE; n++) {
					/* Is fstree[n] a direct child of e? */
					int plen = strlen(e->path);
					char *np = fstree[n].path;
					if(strncmp(np, e->path, plen) != 0)
						continue;
					np += plen;
					if(strcmp(e->path, "/") != 0) {
						if(*np != '/') continue;
						np++;
					}
					if(*np == '\0' || strchr(np, '/') != nil)
						continue;
					/* np is now the direct child name */
					dlen = fs_dir(n, dirbuf, sizeof dirbuf);
					if(dlen <= 0) continue;
					if(cnt < skip) { cnt += dlen; continue; }
					if(pos + dlen > (int)req.count) break;
					memmove(msg_out + pos, dirbuf, dlen);
					pos += dlen;
					cnt += dlen;
				}
				rep.data  = (char*)msg_out;
				rep.count = pos;
			} else {
				/* File read */
				vlong off = req.offset;
				if(off < 0 || off >= (vlong)e->size) {
					rep.count = 0;
				} else {
					n = e->size - (int)off;
					if(n > (int)req.count) n = req.count;
					if(n > (int)sizeof(msg_out)) n = sizeof(msg_out);
					memmove(msg_out, e->data + off, n);
					rep.data  = (char*)msg_out;
					rep.count = n;
				}
			}
			break;
		}

		case Tstat: {
			Fid    *f = fid_lookup(req.fid);
			uchar   sb[256];
			int     slen;
			if(f == nil) { rep.type = Rerror; rep.ename = "unknown fid"; break; }
			slen      = fs_dir(f->fsidx, sb, sizeof sb);
			rep.stat  = sb;
			rep.nstat = slen;
			break;
		}

		case Tclunk: {
			Fid *f = fid_lookup(req.fid);
			fid_free(f);
			break;
		}

		case Tflush:
			/* nothing to flush */
			break;

		default:
			rep.type  = Rerror;
			rep.ename = "not supported";
			break;
		}

		out_len = convS2M(&rep, msg_in, sizeof msg_in);
		if(out_len <= 0) { srv_puts("9pserver: convS2M error\n"); continue; }

		while(sizeof(s->rx.data) - (s->rx.w - s->rx.r) < (uint)out_len)
			;   /* wait for consumer to drain */

		uint rx_w = s->rx.w;
		for(i = 0; i < out_len; i++)
			s->rx.data[(rx_w + i) % sizeof(s->rx.data)] = ((uchar*)msg_in)[i];
		__sync_synchronize();
		s->rx.w = rx_w + out_len;

		microkit_notify(SHARE9P_CH);
	}
}

void
init(void)
{
	Shared9P *s = (Shared9P*)SHARE9P_BASE;
	srv_puts("9pserver: ready\n");
	/* Shared memory is zero-initialized by Microkit — do NOT memset it,
	 * because plan9_root (higher priority) may have already written a
	 * T-message to the tx ring before our init() runs. */
	memset(fids, 0, sizeof(fids));
	/* Process any T-messages that arrived before we were scheduled. */
	handle_9p(s);
}

void
notified(microkit_channel ch)
{
	if(ch == SHARE9P_CH)
		handle_9p((Shared9P*)SHARE9P_BASE);
}
