/*
 * devenv.c — Environment variable device (#e) for Plan 9 on seL4 Microkit.
 *
 * Implements /env/ namespace where each file is an environment variable.
 * Variables are stored as a simple static linked list of (name, value) pairs.
 *
 * Plan 9 convention:
 *   read  /env/name  → reads the current value (as bytes, no NUL terminator)
 *   write /env/name  → sets the variable value
 *   remove /env/name → deletes the variable
 *
 * Initial variables: user, home, path, service, sysname.
 *
 * Under seL4 Microkit, this is a single-process environment with no per-process
 * environment namespace isolation — all variables are global.
 */

#include <u.h>
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "../port/error.h"

/* Maximum number of environment variables */
enum {
	MAXENVVARS = 64,
	MAXENVNAME = 64,
	MAXENVVAL  = 1024,
};

typedef struct Evar Evar;
struct Evar {
	char name[MAXENVNAME];
	char val[MAXENVVAL];
	int  vlen;    /* length of val (may include NUL or not) */
	int  inuse;
};

static Evar envtab[MAXENVVARS];

/* Qid encoding: path = index+1 (0 = directory) */
enum {
	Qenvdir = 0,
};

static void
envreset(void)
{
	int i;
	memset(envtab, 0, sizeof(envtab));

	/* Pre-populate standard Plan 9 environment variables */
	struct { char *name; char *val; } defaults[] = {
		{ "user",    "root"       },
		{ "home",    "/"          },
		{ "service", "sel4"       },
		{ "sysname", "plan9-sel4" },
		{ "path",    "/bin"       },
	};
	for(i = 0; i < (int)nelem(defaults); i++) {
		kstrcpy(envtab[i].name, defaults[i].name, MAXENVNAME);
		kstrcpy(envtab[i].val,  defaults[i].val,  MAXENVVAL);
		envtab[i].vlen  = strlen(defaults[i].val);
		envtab[i].inuse = 1;
	}
}

static void
envinit(void)
{
}

/* Find a variable by name; returns nil if not found */
static Evar*
envfind(char *name)
{
	int i;
	for(i = 0; i < MAXENVVARS; i++)
		if(envtab[i].inuse && strcmp(envtab[i].name, name) == 0)
			return &envtab[i];
	return nil;
}

/* Find a free slot */
static Evar*
envalloc(char *name)
{
	int i;
	for(i = 0; i < MAXENVVARS; i++)
		if(!envtab[i].inuse) {
			kstrcpy(envtab[i].name, name, MAXENVNAME);
			envtab[i].vlen  = 0;
			envtab[i].inuse = 1;
			return &envtab[i];
		}
	return nil;
}

/* Custom devgen for /env/ directory listing */
static int
envgen(Chan *c, char *name, Dirtab *tab, int ntab, int s, Dir *dp)
{
	Evar *ev;
	Qid qid;

	USED(tab); USED(ntab);

	if(s == DEVDOTDOT) {
		devdir(c, (Qid){Qenvdir, 0, QTDIR}, ".", 0, "root", DMDIR|0555, dp);
		return 1;
	}

	if(name != nil) {
		/* Search by name */
		ev = envfind(name);
		if(ev == nil)
			return -1;
		qid.path = (ev - envtab) + 1;
		qid.vers = 0;
		qid.type = QTFILE;
		devdir(c, qid, ev->name, ev->vlen, "root", 0666, dp);
		return 1;
	}

	/* Enumerate: skip deleted/empty slots */
	int n = 0;
	int i;
	for(i = 0; i < MAXENVVARS; i++) {
		if(!envtab[i].inuse)
			continue;
		if(n == s) {
			qid.path = i + 1;
			qid.vers = 0;
			qid.type = QTFILE;
			devdir(c, qid, envtab[i].name, envtab[i].vlen, "root", 0666, dp);
			return 1;
		}
		n++;
	}
	return -1;
}

static Chan*
envattach(char *spec)
{
	return devattach('e', spec);
}

static Walkqid*
envwalk(Chan *c, Chan *nc, char **name, int nname)
{
	return devwalk(c, nc, name, nname, nil, 0, envgen);
}

static int
envstat(Chan *c, uchar *db, int n)
{
	return devstat(c, db, n, nil, 0, envgen);
}

static Chan*
envopen(Chan *c, int omode)
{
	return devopen(c, omode, nil, 0, envgen);
}

static void
envcreate(Chan *c, char *name, int omode, ulong perm)
{
	Evar *ev;

	USED(perm);
	if(c->qid.path != Qenvdir)
		error(Eperm);
	if(openmode(omode) != OWRITE && openmode(omode) != ORDWR)
		error(Eperm);

	ev = envfind(name);
	if(ev == nil)
		ev = envalloc(name);
	if(ev == nil)
		error("env: too many variables");

	c->qid.path = (ev - envtab) + 1;
	c->qid.type = QTFILE;
	c->flag    |= COPEN;
	c->mode     = openmode(omode);
	c->offset   = 0;
	USED(c);
}

static void
envclose(Chan *c)
{
	USED(c);
}

static long
envread(Chan *c, void *buf, long n, vlong off)
{
	Evar *ev;
	long count;

	if(c->qid.type & QTDIR)
		return devdirread(c, buf, n, nil, 0, envgen);

	if(c->qid.path == 0 || c->qid.path > MAXENVVARS)
		error(Enonexist);

	ev = &envtab[c->qid.path - 1];
	if(!ev->inuse)
		error(Enonexist);

	if(off >= ev->vlen)
		return 0;
	count = ev->vlen - off;
	if(count > n)
		count = n;
	memmove(buf, ev->val + off, count);
	return count;
}

static long
envwrite(Chan *c, void *buf, long n, vlong off)
{
	Evar *ev;

	if(c->qid.path == 0 || c->qid.path > MAXENVVARS)
		error(Eperm);

	ev = &envtab[c->qid.path - 1];
	if(!ev->inuse)
		error(Enonexist);

	if(off < 0)
		off = 0;
	if(off + n > MAXENVVAL)
		n = MAXENVVAL - off;
	if(n <= 0)
		return 0;

	memmove(ev->val + off, buf, n);
	if(off + n > ev->vlen)
		ev->vlen = off + n;
	return n;
}

static void
envremove(Chan *c)
{
	Evar *ev;

	if(c->qid.path == 0 || c->qid.path > MAXENVVARS)
		error(Eperm);
	ev = &envtab[c->qid.path - 1];
	if(!ev->inuse)
		error(Enonexist);
	memset(ev, 0, sizeof(Evar));
}

Dev envdevtab = {
	'e',
	"env",

	envreset,
	envinit,
	devshutdown,
	envattach,
	envwalk,
	envstat,
	envopen,
	envcreate,
	envclose,
	envread,
	devbread,
	envwrite,
	devbwrite,
	envremove,
	devwstat,
};
