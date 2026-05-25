/*
 * devcons.c — Console device driver (#c) for Plan 9 on seL4 Microkit.
 *
 * Implements the standard Plan 9 console namespace:
 *   /dev/cons      — read keyboard input / write to UART console
 *   /dev/consctl   — console control (stub)
 *   /dev/null      — /dev/null semantics
 *   /dev/kprint    — kernel print output (alias to cons write)
 *   /dev/reboot    — trigger reboot (stub)
 *
 * Under seL4 Microkit there is no keyboard or interrupt-driven input.
 * Reads from /dev/cons block indefinitely (cooperative spin) or return 0.
 * Writes to /dev/cons, /dev/kprint go to the UART via putstrn().
 */

#include <u.h>
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "../port/error.h"

enum {
	Qdir     = 0,
	Qcons    = 1,
	Qconsctl = 2,
	Qnull    = 3,
	Qkprint  = 4,
	Qreboot  = 5,
};

static Dirtab consdir[] = {
	".",       {Qdir,     0, QTDIR},  0,         DMDIR|0555,
	"cons",    {Qcons,    0, QTFILE}, 0,         0660,
	"consctl", {Qconsctl, 0, QTFILE}, 0,         0220,
	"null",    {Qnull,    0, QTFILE}, 0,         0666,
	"kprint",  {Qkprint,  0, QTFILE}, 0,         0440,
	"reboot",  {Qreboot,  0, QTFILE}, 0,         0220,
};

/* Keyboard ring buffer: filled by consinput(), drained by consread(Qcons) */
static uchar kbuf[256];
static uint  kbuf_r;
static uint  kbuf_w;

/* Rendez that consread sleeps on; woken by consinput() */
static Rendez cons_rdz;

static int
kbuf_hasdata(void *v)
{
	USED(v);
	coherence();
	return kbuf_r != kbuf_w;
}

/*
 * consinput: called from uart_kbd_poll / notified when a key arrives.
 * Safe to call from any context (no lock needed — single writer path
 * under cooperative scheduling).
 */
void
consinput(int c)
{
	kbuf[kbuf_w % sizeof kbuf] = (uchar)c;
	coherence();
	kbuf_w++;
	wakeup(&cons_rdz);
}

static void
consreset(void)
{
}

static void
consinit(void)
{
}

static Chan*
consattach(char *spec)
{
	return devattach('c', spec);
}

static Walkqid*
conswalk(Chan *c, Chan *nc, char **name, int nname)
{
	return devwalk(c, nc, name, nname, consdir, nelem(consdir), devgen);
}

static int
consstat(Chan *c, uchar *db, int n)
{
	return devstat(c, db, n, consdir, nelem(consdir), devgen);
}

static Chan*
consopen(Chan *c, int omode)
{
	return devopen(c, omode, consdir, nelem(consdir), devgen);
}

static void
consclose(Chan *c)
{
	USED(c);
}

static long
consread(Chan *c, void *buf, long n, vlong off)
{
	if(c->qid.type & QTDIR)
		return devdirread(c, buf, n, consdir, nelem(consdir), devgen);

	switch((int)c->qid.path) {
	case Qcons: {
		long cnt = 0;
		USED(off);
		sleep(&cons_rdz, kbuf_hasdata, nil);
		while(cnt < n && kbuf_hasdata(nil))
			((uchar*)buf)[cnt++] = kbuf[kbuf_r++ % sizeof kbuf];
		return cnt;
	}

	case Qnull:
		return 0;

	case Qkprint:
		/* Kernel print buffer not implemented — return empty */
		USED(off);
		return 0;

	default:
		error(Eperm);
	}
	return 0;
}

static long
conswrite(Chan *c, void *buf, long n, vlong off)
{
	USED(off);

	switch((int)c->qid.path) {
	case Qcons:
	case Qkprint:
		/* Write directly to UART */
		putstrn((char*)buf, n);
		return n;

	case Qconsctl:
		/* Console control — ignore all commands silently */
		return n;

	case Qnull:
		/* /dev/null — silently discard */
		return n;

	case Qreboot:
		/* Trigger a reboot stub */
		putstrn("reboot requested\n", 17);
		reboot(nil, nil, 0);
		return n;

	default:
		error(Eperm);
	}
	return 0;
}

Dev consdevtab = {
	'c',
	"cons",

	consreset,
	consinit,
	devshutdown,
	consattach,
	conswalk,
	consstat,
	consopen,
	devcreate,
	consclose,
	consread,
	devbread,
	conswrite,
	devbwrite,
	devremove,
	devwstat,
};
