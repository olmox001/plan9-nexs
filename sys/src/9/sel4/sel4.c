#include <u.h>
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"

/*
 * devtab: Master device driver table for Plan 9 on seL4 Microkit.
 *
 * Device characters (Plan 9 convention):
 *   '/' — root filesystem (devroot)
 *   'm' — seL4 9P mount transport (devmntsel4)
 *   'c' — console I/O (devcons)
 *   'e' — environment variables (devenv)
 *   '|' — anonymous pipes (devpipe)
 *   'd' — fd namespace / dup (devdup)
 *   's' — service registry for mount points (devsrv)
 *   'i' — graphics draw device (devdraw)
 *   'M' — mouse/tablet input (devmouse)
 */

extern Dev rootdevtab;
extern Dev mntsel4devtab;
extern Dev consdevtab;
extern Dev envdevtab;
extern Dev pipedevtab;
extern Dev dupdevtab;
extern Dev srvdevtab;
extern Dev drawdevtab;
extern Dev mousedevtab;

Dev* devtab[] = {
	&rootdevtab,
	&mntsel4devtab,
	&consdevtab,
	&envdevtab,
	&pipedevtab,
	&dupdevtab,
	&srvdevtab,
	&drawdevtab,
	&mousedevtab,
	nil,
};

char* conffile = "sel4";
