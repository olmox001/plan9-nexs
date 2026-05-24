/*
 * devmouse.c — Mouse device (#M) for Plan 9 on seL4 Microkit.
 *
 * Exports /dev/mouse and /dev/mousectl.
 * Mouse events arrive from input_pd via a shared ring buffer at SEL4_INPUT_BASE.
 * The input_pd sends a seL4 notification on channel SEL4_INPUT_CH when new
 * events are available; the notified() callback in main.c calls mouseinput().
 *
 * Event format (Plan 9 standard): "m%11d%11d%11d%11d\n"
 *   m<x><y><buttons><msec>
 */

#include <u.h>
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "../port/error.h"

#define SEL4_INPUT_BASE   0x40000000ULL
#define SEL4_INPUT_CH     3
#define MOUSE_BUF         4096

enum {
	Qdir      = 0,
	Qmouse    = 1,
	Qmousectl = 2,
};

static Dirtab mousedir[] = {
	".",        {Qdir,      0, QTDIR},  0,  DMDIR|0555,
	"mouse",    {Qmouse,    0, QTFILE}, 0,  0600,
	"mousectl", {Qmousectl, 0, QTFILE}, 0,  0200,
};

/* Ring buffer for raw mouse messages from input_pd */
typedef struct InputRing InputRing;
struct InputRing {
	volatile uint w;
	volatile uint r;
	volatile uchar data[4096];
};

static InputRing *inring(void) {
	return (InputRing*)(uintptr)SEL4_INPUT_BASE;
}

/* Local cooked mouse state */
static struct {
	Lock;
	Rendez r;
	int    x, y, buttons;
	ulong  msec;
	int    fresh;   /* 1 if a new event is waiting */
} mouse;

/* mouseinput: called from notified() when input_pd sends a notification */
void
mouseinput(void)
{
	InputRing *ir = inring();
	uchar     buf[32];
	uint      avail, i, n;

	while(ir->w != ir->r) {
		avail = ir->w - ir->r;
		if(avail < 1)
			break;
		n = avail > sizeof(buf) ? sizeof(buf) : avail;
		for(i = 0; i < n; i++)
			buf[i] = ir->data[(ir->r + i) % sizeof(ir->data)];
		coherence();
		ir->r += n;
		/* Parse virtio tablet event: 4-byte x, 4-byte y, 4-byte buttons */
		if(n >= 12) {
			lock(&mouse);
			mouse.x       = (int)(buf[0] | (buf[1]<<8) | (buf[2]<<16) | (buf[3]<<24));
			mouse.y       = (int)(buf[4] | (buf[5]<<8) | (buf[6]<<16) | (buf[7]<<24));
			mouse.buttons = (int)(buf[8] | (buf[9]<<8) | (buf[10]<<16) | (buf[11]<<24));
			mouse.fresh   = 1;
			unlock(&mouse);
			wakeup(&mouse.r);
		}
	}
}

static int
mousefresh(void *arg)
{
	USED(arg);
	return mouse.fresh;
}

static void
mousereset(void)
{
	memset(&mouse, 0, sizeof(mouse));
	memset(inring(), 0, sizeof(InputRing));
}

static void
mouseinit(void)
{
}

static Chan*
mouseattach(char *spec)
{
	return devattach('M', spec);
}

static Walkqid*
mousewalk(Chan *c, Chan *nc, char **name, int nname)
{
	return devwalk(c, nc, name, nname, mousedir, nelem(mousedir), devgen);
}

static int
mousestat(Chan *c, uchar *db, int n)
{
	return devstat(c, db, n, mousedir, nelem(mousedir), devgen);
}

static Chan*
mouseopen(Chan *c, int omode)
{
	return devopen(c, omode, mousedir, nelem(mousedir), devgen);
}

static void
mouseclose(Chan *c)
{
	USED(c);
}

static long
mouseread(Chan *c, void *buf, long n, vlong off)
{
	char msg[64];
	int  m;

	USED(off);
	if(c->qid.type & QTDIR)
		return devdirread(c, buf, n, mousedir, nelem(mousedir), devgen);

	switch((int)c->qid.path) {
	case Qmouse:
		/* Block until a fresh event arrives */
		sleep(&mouse.r, mousefresh, nil);
		lock(&mouse);
		mouse.fresh = 0;
		m = snprint(msg, sizeof msg, "m%11d%11d%11d%11d\n",
			mouse.x, mouse.y, mouse.buttons, (int)mouse.msec);
		unlock(&mouse);
		if(m > n) m = (int)n;
		memmove(buf, msg, m);
		return m;

	case Qmousectl:
		return 0;

	default:
		error(Eperm);
	}
	return 0;
}

static long
mousewrite(Chan *c, void *buf, long n, vlong off)
{
	char msg[64];
	int  x, y, b;

	USED(off);
	switch((int)c->qid.path) {
	case Qmouse: {
		/* Accept "m<x><y><buttons>" to warp cursor */
		char *p;
		int  cn;
		cn = n < (long)(sizeof(msg)-1) ? (int)n : (int)(sizeof(msg)-1);
		memmove(msg, buf, cn);
		msg[cn] = '\0';
		if(msg[0] == 'm') {
			p = msg + 1;
			x = (int)strtol(p, &p, 10);
			y = (int)strtol(p, &p, 10);
			b = (int)strtol(p, &p, 10);
			lock(&mouse);
			mouse.x = x;
			mouse.y = y;
			mouse.buttons = b;
			mouse.fresh = 1;
			unlock(&mouse);
			wakeup(&mouse.r);
		}
		return n;
	}

	case Qmousectl:
		/* Ignore control commands */
		return n;

	default:
		error(Eperm);
	}
	return 0;
}

Dev mousedevtab = {
	'M',
	"mouse",

	mousereset,
	mouseinit,
	devshutdown,
	mouseattach,
	mousewalk,
	mousestat,
	mouseopen,
	devcreate,
	mouseclose,
	mouseread,
	devbread,
	mousewrite,
	devbwrite,
	devremove,
	devwstat,
};
