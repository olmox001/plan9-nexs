#include <u.h>
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#define fault  microkit_fault
#define strcpy microkit_strcpy
#include <microkit.h>
#undef fault
#undef strcpy

extern Dev mntsel4devtab;
extern Dev rootdevtab;
extern Dev consdevtab;
extern Dev envdevtab;
extern Dev pipedevtab;
extern Dev dupdevtab;
extern Dev srvdevtab;
extern Dev drawdevtab;
extern Dev mousedevtab;

/* syscall.c */
extern void svc_init(void);

/* ── Kernel panic ─────────────────────────────────────────────────────────── */
void
panic(char *fmt, ...)
{
	va_list ap;
	char    buf[256];

	va_start(ap, fmt);
	vseprint(buf, buf + sizeof buf, fmt, ap);
	va_end(ap);

	putstrn("PANIC: ", 7);
	putstrn(buf, strlen(buf));
	putstrn("\n", 1);
	while(1);
}

void
printinit(void)
{
	coherence();
}

void
plan9_microkit_notify(int ch)
{
	microkit_notify(ch);
}

/*
 * uart_kbd_poll: drain the PL011 RX FIFO and feed characters to consinput().
 * Called both from the UART IRQ notified handler and as a polling fallback
 * in microkit_idle_wait so UART-typed characters reach cons_rdz promptly.
 * PL011 registers: DR at offset 0x00, FR at offset 0x18 (index 6).
 *   FR bit 4 = RXFE (receive FIFO empty).
 */
static void
uart_kbd_poll(void)
{
	volatile u32int *uart = (volatile u32int*)0x09000000;
	while(!(uart[6] & (1u << 4)))
		consinput((int)(uart[0] & 0xff));
}

/*
 * microkit_idle_wait: called from sched.c when no Plan 9 proc is ready.
 * Polls UART for keyboard input, then blocks at the seL4 level (via
 * seL4_Wait) so other PDs (9pserver, display, input) get CPU time.
 * On return, dispatches each set badge bit as a channel notification.
 */
void
microkit_idle_wait(void)
{
	seL4_Word badge = 0;
	int ch;
	uart_kbd_poll();
	if(anyready())
		return;
	seL4_Wait(1, &badge);
	for(ch = 0; badge != 0; ch++, badge >>= 1)
		if(badge & 1)
			notified((microkit_channel)ch);
}

/* ── Namespace bootstrap ──────────────────────────────────────────────────── */

/*
 * devsinit: Initialize all device drivers by calling their reset() and init()
 * functions in devtab order.
 */
static void
devsinit(void)
{
	Dev **d;

	for(d = devtab; *d != nil; d++) {
		if((*d)->reset != nil)
			(*d)->reset();
		if((*d)->init != nil)
			(*d)->init();
	}
}

/*
 * fdinit: Set up stdio (fd 0, 1, 2) for the init process as /dev/cons.
 * Returns 0 on success, -1 on error.
 */
static int
fdinit(Proc *p)
{
	Chan *c;
	int   i;

	/* Allocate file group for init */
	p->fgrp = allocfgrp();

	/* Attach to #c (cons device) */
	c = consdevtab.attach("");
	if(c == nil)
		return -1;

	/* Open /dev/cons */
	{
		char *ep[] = { "cons" };
		Walkqid *wq = consdevtab.walk(c, nil, ep, 1);
		if(wq == nil) { cclose(c); return -1; }
		c->qid = wq->qid[0];
	}
	consdevtab.open(c, ORDWR);

	/* Install as fd 0, 1, 2 */
	c->ref_member.ref += 2;   /* three references total */
	p->fgrp->fd[0] = c;
	p->fgrp->fd[1] = c;
	p->fgrp->fd[2] = c;
	p->fgrp->maxfd = 2;
	USED(i);
	return 0;
}

/*
 * init9: The Plan 9 init process.  Sets up the namespace then execs rc.
 */
static void
init9(void *arg)
{
	USED(arg);

	putstrn("Plan 9: namespace init\n", 23);

	/* Set up /env */
	{
		extern Dev envdevtab;
		envdevtab.reset();
	}

	/* Bind /dev (consdevtab) into the namespace */
	putstrn("Plan 9: devices ready\n", 22);

	/* Set up stdin/stdout/stderr */
	if(fdinit(up) < 0) {
		putstrn("Plan 9: fdinit failed\n", 22);
		pexit("fdinit", 0);
	}

	putstrn("Plan 9: execing rc\n", 19);

	/* Verify 9P rootfs: read /rc/bin/termrc and print it */
	if(waserror()) {
		putstrn("Plan 9: termrc read failed\n", 27);
		poperror();
	} else {
		Chan *tc = namec("/rc/bin/termrc", Aopen, OREAD, 0);
		char tbuf[256];
		long tn = devtab[tc->type]->read(tc, tbuf, sizeof(tbuf)-1, 0);
		if(tn > 0) {
			tbuf[tn] = '\0';
			putstrn("termrc: ", 8);
			putstrn(tbuf, tn);
		}
		cclose(tc);
		poperror();
	}

	/* Exec the rc shell from the rootfs */
	{
		char *rcargs[] = { "/bin/rc", "-l", nil };
		if(waserror()) {
			putstrn("Plan 9: exec rc failed: ", 24);
			putstrn(up->syserrstr, strlen(up->syserrstr));
			putstrn("\n", 1);
			/* Fall back to a simple REPL */
			goto repl;
		}
		exec9p("/bin/rc", rcargs);
		poperror();
	}
	pexit("init9 done", 1);

repl:
	/* Minimal fallback: echo lines from /dev/cons */
	{
		char line[256];
		int  n;
		Chan *cc;
		char *ep[] = { "cons" };
		Walkqid *wq;

		cc = consdevtab.attach("");
		wq = consdevtab.walk(cc, nil, ep, 1);
		if(wq != nil) {
			cc->qid = wq->qid[0];
			consdevtab.open(cc, ORDWR);
		}
		for(;;) {
			consdevtab.write(cc, "% ", 2, 0);
			n = consdevtab.read(cc, line, sizeof line - 1, 0);
			if(n > 0) {
				line[n] = '\0';
				consdevtab.write(cc, line, n, 0);
			}
		}
	}
}

/* ── Microkit entry points ────────────────────────────────────────────────── */

void
init(void)
{
	/* 1. UART */
	uart_init();
	putstrn("Plan 9 on seL4\n", 15);

	/* 2. Syscall gate — must be before any user process */
	svc_init();

	/* 3. Processor and memory */
	machinit();
	confinit();
	xinit();
	printinit();

	/* 4. Scheduler */
	procinit();

	/* 5. Initialize all devices */
	devsinit();

	putstrn("Plan 9: boot\n", 13);

	/* 6. Create init process */
	kproc("init9", init9, nil);

	/* 7. Start cooperative scheduler — never returns */
	schedinit();
}

/*
 * notified: Dispatches seL4 channel notifications to the right handler.
 *   ch == 1: 9P server (rootfs) — handled by devmntsel4 poll
 *   ch == 2: display_pd flush ACK
 *   ch == 3: input_pd mouse event
 */
void
notified(microkit_channel ch)
{
	switch(ch) {
	case 1:
		/* 9pserver posted an R-message; wake the sleeping mntsel4read */
		mnt9p_wakeup();
		break;
	case 3:
		/* Mouse/keyboard event from input_pd */
		mouseinput();
		break;
	case 4:
		/* UART RX IRQ — drain RX FIFO into cons keyboard buffer */
		uart_kbd_poll();
		microkit_irq_ack(4);
		break;
	/* ch == 2: display flush ACK — no action needed in plan9_root */
	}
}
