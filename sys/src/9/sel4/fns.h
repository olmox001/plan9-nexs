#ifndef _SEL4_FNS_H_
#define _SEL4_FNS_H_

#include "portfns.h"

/* ── Cooperative interrupt control ──────────────────────────────────────── */
extern int  splhi(void);
extern int  spllo(void);
extern void splx(int);
extern int  splfhi(void);
extern void splflo(void);

/* ── Context switch (setlabel/gotolabel) ─────────────────────────────────── */
extern int  setlabel(Label*);
extern void gotolabel(Label*);

/* ── Coherence and timing ────────────────────────────────────────────────── */
extern void coherence(void);
extern void idlehands(void);
#define cycles(ip) *(ip) = 0

/* ── MMU address translation (identity mapping under seL4 PD) ──────────── */
#define PADDR(a)   ((uintptr)(a))
#define KADDR(a)   ((void*)(a))
#define VA(k)      ((uintptr)(k))

/* ── Memory and processor initialization ────────────────────────────────── */
extern void confinit(void);
extern void machinit(void);
extern void meminit(void);
extern void links(void);

/* ── Console UART driver ─────────────────────────────────────────────────── */
extern void uart_init(void);
extern void putstrn(char*, int);

/* ── MMU/page table stubs ───────────────────────────────────────────────── */
extern void putasid(Proc*);
extern void procfork(Proc*);
extern void procsetup(Proc*);
extern void procsave(Proc*);
extern void procrestore(Proc*);
extern void mmuswitch(Proc*);
extern void mmurelease(Proc*);

/* ── Plan 9 cooperative scheduler (sched.c) ─────────────────────────────── */
extern void  procinit(void);
extern Proc* allocproc(void);
extern void  kproc(char*, void(*)(void*), void*);
extern void  kprocchild(Proc*, void(*)(void));
extern void  ready(Proc*);
extern Proc* runproc(void);
extern int   anyready(void);
extern void  schedinit(void) __attribute__((noreturn));
extern void  sched(void);
extern void  sleep(Rendez*, int(*)(void*), void*);
extern Proc* wakeup(Rendez*);
extern void  tsleep(Rendez*, int(*)(void*), void*, ulong);
extern void  pexit(char*, int) __attribute__((noreturn));
extern void  resched(char*);
extern void  hzsched(void);

/* ── File descriptor group (fgrp.c) ─────────────────────────────────────── */
extern Fgrp* allocfgrp(void);
extern void  closefgrp(Fgrp*);
extern void  forceclosefgrp(void);
extern Chan* fdtochan(int, int, int, int);
extern int   newfd(Chan*, int);
extern void  fdclose(int, int);
extern Fgrp* dupfgrp(Fgrp*);

/* ── ELF loader / exec (exec.c) ─────────────────────────────────────────── */
extern Proc* exec9p(char*, char**);
extern Chan* namec(char*, int, int, ulong);

/* ── String buffer for devdup ────────────────────────────────────────────── */
/* up->genbuf is in portdat.h Proc struct; declared here as reminder */

/* ── Mouse input handler (called from notified) ──────────────────────────── */
extern void mouseinput(void);

/* ── 9P transport wakeup (called from notified when 9pserver replies) ─────── */
extern void mnt9p_wakeup(void);

/* ── seL4 notification wrapper ───────────────────────────────────────────── */
extern void plan9_microkit_notify(int);

/* ── Chan error string helper ───────────────────────────────────────────── */
extern void kstrcpy(char*, char*, int);
extern int  rerrstr(char*, uint);

/* readstr and procfdprint are declared in portfns.h with their canonical sigs */

#endif /* _SEL4_FNS_H_ */
