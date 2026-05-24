/*
 * devdraw.c — Draw device (#i) for Plan 9 on seL4 Microkit.
 *
 * Implements the Plan 9 draw(3) interface.  Graphics operations are applied
 * to a software framebuffer in the shared memory region, then flushed to the
 * display_pd PD via a seL4 notification channel.
 *
 * Namespace under #i:
 *   /dev/draw        — primary draw channel (client reads/writes draw ops)
 *   /dev/screen      — raw framebuffer image (read-only)
 *   /dev/cursor      — cursor image and hot-spot
 *   /dev/winname     — window manager name
 *
 * Framebuffer layout in shared memory (SEL4_DRAW_BASE):
 *   uint32 width, height, depth     — display geometry
 *   uint8  pixels[height][width*4]  — BGRA8888 pixels
 *
 * Protocol to display_pd (channel SEL4_DRAW_CH):
 *   After writing pixels, plan9_root sends a notification.
 *   display_pd DMA-flushes the region to the GPU.
 */

#include <u.h>
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "../port/error.h"

#define SEL4_DRAW_BASE    0x30000000ULL   /* framebuffer shared region */
#define SEL4_DRAW_CH      2               /* seL4 notification channel to display_pd */
#define DRAW_WIDTH        1024
#define DRAW_HEIGHT       768
#define DRAW_DEPTH        32              /* bits per pixel */
#define DRAW_STRIDE       (DRAW_WIDTH * 4)
#define DRAW_FBSIZE       (DRAW_HEIGHT * DRAW_STRIDE)

enum {
	Qdir      = 0,
	Qdraw     = 1,
	Qscreen   = 2,
	Qcursor   = 3,
	Qwinname  = 4,
};

static Dirtab drawdir[] = {
	".",       {Qdir,     0, QTDIR},   0,              DMDIR|0555,
	"draw",    {Qdraw,    0, QTFILE},  0,              0600,
	"screen",  {Qscreen,  0, QTFILE},  DRAW_FBSIZE,    0444,
	"cursor",  {Qcursor,  0, QTFILE},  0,              0600,
	"winname", {Qwinname, 0, QTFILE},  0,              0444,
};

/* Framebuffer header at the base of shared memory */
typedef struct FBHeader FBHeader;
struct FBHeader {
	uint   w;
	uint   h;
	uint   depth;
	uint   stride;
};

static uchar *fbbase(void) { return (uchar*)(uintptr)SEL4_DRAW_BASE; }
static FBHeader *fbhdr(void) { return (FBHeader*)fbbase(); }
static uchar *fbpix(void) { return fbbase() + sizeof(FBHeader); }

static char winname[] = "plan9-sel4";

/* Flush the framebuffer: notify display_pd */
static void
drawflush(void)
{
	coherence();
	plan9_microkit_notify(SEL4_DRAW_CH);
}

static void
drawreset(void)
{
	FBHeader *hdr = fbhdr();
	hdr->w      = DRAW_WIDTH;
	hdr->h      = DRAW_HEIGHT;
	hdr->depth  = DRAW_DEPTH;
	hdr->stride = DRAW_STRIDE;
	/* Clear framebuffer to black */
	memset(fbpix(), 0, DRAW_FBSIZE);
	drawflush();
}

static void
drawinit(void)
{
}

static Chan*
drawattach(char *spec)
{
	return devattach('i', spec);
}

static Walkqid*
drawwalk(Chan *c, Chan *nc, char **name, int nname)
{
	return devwalk(c, nc, name, nname, drawdir, nelem(drawdir), devgen);
}

static int
drawstat(Chan *c, uchar *db, int n)
{
	return devstat(c, db, n, drawdir, nelem(drawdir), devgen);
}

static Chan*
drawopen(Chan *c, int omode)
{
	return devopen(c, omode, drawdir, nelem(drawdir), devgen);
}

static void
drawclose(Chan *c)
{
	USED(c);
}

/*
 * drawread: Read from /dev/screen returns raw pixel data.
 * Reads from /dev/winname return the window manager name.
 * /dev/draw and /dev/cursor reads return display geometry info.
 */
static long
drawread(Chan *c, void *buf, long n, vlong off)
{
	char info[128];
	int  m;

	if(c->qid.type & QTDIR)
		return devdirread(c, buf, n, drawdir, nelem(drawdir), devgen);

	switch((int)c->qid.path) {
	case Qscreen:
		/* Read raw pixels from the framebuffer */
		if(off < 0)
			off = 0;
		if(off >= DRAW_FBSIZE)
			return 0;
		m = DRAW_FBSIZE - (int)off;
		if(m > n)
			m = n;
		memmove(buf, fbpix() + off, m);
		return m;

	case Qwinname:
		return readstr((ulong)off, buf, n, winname);

	case Qdraw:
		/* Return display info: width height depth */
		snprint(info, sizeof info, "%d %d %d\n",
			DRAW_WIDTH, DRAW_HEIGHT, DRAW_DEPTH);
		return readstr((ulong)off, buf, n, info);

	case Qcursor:
		return 0;

	default:
		error(Eperm);
	}
	return 0;
}

/*
 * drawwrite: Write to /dev/draw executes a drawing command.
 *
 * Simple command format (text, newline-terminated):
 *   fill <x> <y> <w> <h> <rrggbb>   — fill rectangle
 *   blit <x> <y> <w> <h>            — followed by w*h*4 bytes of pixel data
 *   flush                            — flush framebuffer to display
 *   clear                            — fill screen with black
 *
 * Real Plan 9 uses a binary protocol for performance; this text format is
 * for initial bootstrap.  rio and libdraw use the full binary protocol.
 */
static long
drawwrite(Chan *c, void *buf, long n, vlong off)
{
	char  cmd[256];
	int   x, y, w, h;
	ulong color;
	int   cmdlen;

	USED(off);

	if(c->qid.type & QTDIR)
		error(Eperm);

	switch((int)c->qid.path) {
	case Qdraw:
		cmdlen = n < (long)sizeof(cmd)-1 ? (int)n : (int)sizeof(cmd)-1;
		memmove(cmd, buf, cmdlen);
		cmd[cmdlen] = '\0';

		if(strncmp(cmd, "flush", 5) == 0) {
			drawflush();
		} else if(strncmp(cmd, "clear", 5) == 0) {
			memset(fbpix(), 0, DRAW_FBSIZE);
			drawflush();
		} else if(strncmp(cmd, "fill ", 5) == 0) {
			/* parse: "fill x y w h rrggbb" */
			char *p = cmd + 5;
			x     = (int)strtol(p, &p, 10);
			y     = (int)strtol(p, &p, 10);
			w     = (int)strtol(p, &p, 10);
			h     = (int)strtol(p, &p, 10);
			color = strtoul(p, nil, 16);
			{
				int   row, col;
				uchar r = (color >> 16) & 0xff;
				uchar g = (color >> 8)  & 0xff;
				uchar b = (color)       & 0xff;
				for(row = y; row < y+h && row < DRAW_HEIGHT; row++)
					for(col = x; col < x+w && col < DRAW_WIDTH; col++) {
						uchar *px = fbpix() + row * DRAW_STRIDE + col * 4;
						px[0] = b; px[1] = g; px[2] = r; px[3] = 0xff;
					}
			}
		}
		return n;

	case Qscreen:
		/* Direct pixel write to framebuffer */
		if(off < 0) off = 0;
		if(off >= DRAW_FBSIZE) return 0;
		{
			long m = DRAW_FBSIZE - (long)off;
			if(m > n) m = n;
			memmove(fbpix() + off, buf, m);
			return m;
		}

	case Qcursor:
		/* Accept cursor data silently */
		return n;

	default:
		error(Eperm);
	}
	return 0;
}

Dev drawdevtab = {
	'i',
	"draw",

	drawreset,
	drawinit,
	devshutdown,
	drawattach,
	drawwalk,
	drawstat,
	drawopen,
	devcreate,
	drawclose,
	drawread,
	devbread,
	drawwrite,
	devbwrite,
	devremove,
	devwstat,
};
