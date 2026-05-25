/*
 * exec.c — ELF64 AArch64 process loader for Plan 9 on seL4 Microkit.
 *
 * exec() loads an ELF64 binary from the filesystem, creates a new Plan 9
 * Proc with its own stack, and makes it ready to run.
 *
 * Since we run in a single-address-space PD, there is no MMU isolation
 * between processes.  All ELF PT_LOAD segments are mapped into the shared
 * address space.  This is safe for trusted kernel processes (rc, rio, etc.)
 * loaded from our read-only rootfs.
 *
 * Entrypoint calling convention (AArch64 SysV ELF):
 *   x0 = argc, x1 = argv[], x2 = envp[]
 *
 * After loading, exec() creates a new Plan 9 Proc that starts at the ELF
 * entry point.  The current proc continues running; exec() does NOT replace
 * the current proc.  Call pexit() afterward if you want fork+exec semantics.
 */

#include <u.h>
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"
#include "../port/error.h"
#include "include/tos.h"

/* Forward declaration of kernel syscall dispatcher (syscall.c) */
extern long syscall_dispatch(long sysno, long a0, long a1, long a2,
                              long a3, long a4, long a5);
/* Global Tos pointer — written here, read by syscall_dispatch for errstr. */
extern Tos *currenttos;

/* Minimal ELF64 header structures */
typedef struct Elf64Hdr Elf64Hdr;
struct Elf64Hdr {
	uchar ident[16];
	u16int type;
	u16int machine;
	u32int version;
	uvlong entry;
	uvlong phoff;
	uvlong shoff;
	u32int flags;
	u16int ehsize;
	u16int phentsize;
	u16int phnum;
	u16int shentsize;
	u16int shnum;
	u16int shstrndx;
};

typedef struct Elf64Phdr Elf64Phdr;
struct Elf64Phdr {
	u32int type;
	u32int flags;
	uvlong offset;
	uvlong vaddr;
	uvlong paddr;
	uvlong filesz;
	uvlong memsz;
	uvlong align;
};

enum {
	PT_LOAD    = 1,
	ET_EXEC    = 2,
	ET_DYN     = 3,
	EM_AARCH64 = 183,
	EI_CLASS   = 4,
	ELFCLASS64 = 2,
	EI_DATA    = 5,
	ELFDATA2LSB = 1,
};

/* Trampoline: jumps to ELF entry with argc/argv/envp on the process stack. */
typedef struct ExecCtx ExecCtx;
struct ExecCtx {
	uvlong entry;
	int    argc;
	char **argv;
	char **envp;
};

static void
exectrampoline(void *arg)
{
	ExecCtx *ctx = (ExecCtx*)arg;
	Tos     *tos;
	uintptr  sp;
	char    *ustack;
	int      argc, i;
	char   **argv;

	argc = ctx->argc;
	argv = ctx->argv;

	/* Allocate user stack (256KB is plenty and avoids heap exhaustion) */
	ulong stack_sz = 256 * 1024;
	ustack = (char*)xalloc(stack_sz);
	if(ustack == nil)
		panic("exectrampoline: no stack memory");

	/* Place Tos at the top of the stack (Plan 9 ABI) */
	sp = (uintptr)(ustack + stack_sz);
	sp = STACKALIGN(sp);
	sp -= sizeof(Tos);
	tos = (Tos*)sp;
	memset(tos, 0, sizeof(Tos));

	/* Install syscall gate — direct function pointer into plan9_root */
	tos->gate = syscall_dispatch;
	currenttos = tos;

	/* Process identity */
	tos->pid  = up ? up->pid : 0;
	tos->ppid = up && up->parent ? up->parent->pid : 0;

	/* Argument vector */
	tos->argc = argc;
	tos->argv = argv;
	tos->envp = nil;

	/* Align stack below Tos (16-byte aligned per AArch64 ABI) */
	sp = STACKALIGN(sp - 8);

	/* Call ELF entry with R0 = Tos* (Plan 9 AArch64 ABI) */
	typedef void (*EntryFn)(Tos *tos);
	EntryFn fn = (EntryFn)(uintptr)ctx->entry;
	fn(tos);

	USED(i);
	pexit("exec done", 1);
}


/*
 * exec9p: Load an ELF binary from the Plan 9 filesystem at path.
 * args[0] = program name, args[1..] = arguments, args terminated by nil.
 * Creates a new Plan 9 Proc and makes it ready.
 * Returns the new Proc, or panics on failure.
 */
Proc*
exec9p(char *path, char **args)
{
	Chan       *c;
	Elf64Hdr    hdr;
	Elf64Phdr  *phdrs;
	int         nph, n, i;
	uvlong      entry;
	ExecCtx    *ctx;
	Proc       *p;
	int         argc;

	/* Open the binary — let namec's error() propagate to the caller */
	c = namec(path, Aopen, OREAD, 0);

	if(waserror()) {
		cclose(c);
		nexterror();
	}

	/* Read ELF header */
	n = devtab[c->type]->read(c, &hdr, sizeof(hdr), 0);
	if(n < (int)sizeof(hdr))
		error("exec9p: short ELF header read");
	if(hdr.ident[0] != 0x7f || hdr.ident[1] != 'E' ||
	   hdr.ident[2] != 'L'  || hdr.ident[3] != 'F')
		error("exec9p: not an ELF file");
	if(hdr.ident[EI_CLASS] != ELFCLASS64)
		error("exec9p: not ELF64");
	if(hdr.machine != EM_AARCH64)
		error("exec9p: not AArch64");
	if(hdr.type != ET_EXEC && hdr.type != ET_DYN)
		error("exec9p: not executable");

	entry = hdr.entry;
	nph   = hdr.phnum;

	if(nph == 0 || hdr.phentsize != sizeof(Elf64Phdr))
		error("exec9p: bad program header");

	/* Read program headers */
	phdrs = (Elf64Phdr*)xalloc(nph * sizeof(Elf64Phdr));
	if(phdrs == nil)
		error("exec9p: out of memory for phdrs");

	n = devtab[c->type]->read(c, phdrs, nph * sizeof(Elf64Phdr), hdr.phoff);
	if(n < (int)(nph * sizeof(Elf64Phdr))) {
		xfree(phdrs);
		error("exec9p: short phdr read");
	}

	/* Load PT_LOAD segments */
	for(i = 0; i < nph; i++) {
		Elf64Phdr *ph = &phdrs[i];
		uchar     *dst;

		if(ph->type != PT_LOAD)
			continue;
		if(ph->memsz == 0)
			continue;

		dst = (uchar*)(uintptr)ph->vaddr;
		/* Zero-fill the whole segment (BSS) */
		memset(dst, 0, ph->memsz);
		/* Load file data */
		if(ph->filesz > 0) {
			n = devtab[c->type]->read(c, dst, ph->filesz, ph->offset);
			if(n < (int)ph->filesz) {
				xfree(phdrs);
				error("exec9p: short segment read");
			}
		}
	}
	xfree(phdrs);

	/* Count argc */
	for(argc = 0; args != nil && args[argc] != nil; argc++)
		;

	/* Build context for the trampoline proc */
	ctx        = (ExecCtx*)xalloc(sizeof(ExecCtx));
	ctx->entry = entry;
	ctx->argc  = argc;
	ctx->argv  = args;
	ctx->envp  = nil;

	poperror();
	cclose(c);

	/* Allocate and start the new proc */
	p = allocproc();
	if(p == nil)
		error("exec9p: out of procs");
	p->text = path;
	p->kpfun  = exectrampoline;
	p->kparg  = ctx;
	if(up != nil && up->fgrp != nil)
		p->fgrp = dupfgrp(up->fgrp);
	else
		p->fgrp = allocfgrp();
	if(up != nil && up->dot != nil) {
		p->dot = up->dot;
		p->dot->ref_member.ref++;
	}
	p->syserrstr = p->errbuf0;
	p->errstr    = p->errbuf1;
	ready(p);
	return p;
}

/*
 * namec: Walk the VFS to open a file.
 * Simplified: only handles absolute paths from devtab.
 * Real Plan 9 namec is in port/chan.c.
 */
Chan*
namec(char *name, int amode, int omode, ulong perm)
{
	Dev  *d;
	Chan *c;
	char *path;
	char *elems[32];
	int   nelem, i;
	char  buf[512];

	USED(amode); USED(perm);

	putstrn("namec: ", 7);
	putstrn(name ? name : "(nil)", name ? strlen(name) : 5);
	putstrn("\n", 1);

	if(name == nil)
		error("namec: nil path");

	/* Handle #X device-special paths (e.g. #d/0, #c/cons, #e/user) */
	if(name[0] == '#') {
		char  dc2;
		char  buf2[512];
		char *elems2[32];
		int   nelem2, i2;
		char *p2, *q2;

		dc2 = name[1];
		d = nil;
		for(i2 = 0; devtab[i2] != nil; i2++) {
			if(devtab[i2]->dc == dc2) {
				d = devtab[i2];
				break;
			}
		}
		if(d == nil)
			error("namec: unknown device");

		c = d->attach("");
		if(c == nil)
			error("namec: attach failed");

		/* Walk the path after '#X', skipping the optional leading '/' */
		p2 = (char*)name + 2;
		if(*p2 == '/') p2++;

		if(*p2 != '\0') {
			kstrcpy(buf2, p2, sizeof buf2);
			nelem2 = 0;
			p2 = buf2;
			while(*p2 && nelem2 < 32) {
				q2 = strchr(p2, '/');
				if(q2 != nil) *q2 = '\0';
				if(*p2) elems2[nelem2++] = p2;
				if(q2 == nil) break;
				p2 = q2 + 1;
			}
			if(nelem2 > 0) {
				Walkqid *wq2 = d->walk(c, nil, elems2, nelem2);
				if(wq2 == nil || wq2->nqid != nelem2) {
					cclose(c);
					error("namec: #X path not found");
				}
				c->qid = wq2->qid[nelem2 - 1];
			}
		}
		c = d->open(c, omode);
		return c;
	}

	if(name[0] != '/')
		error("namec: relative paths not supported");

	/* Find device by first path element or '#' prefix */
	kstrcpy(buf, name, sizeof buf);
	path = buf + 1;

	/* Split path into elements */
	nelem = 0;
	char *p = path;
	while(*p && nelem < 32) {
		char *q = strchr(p, '/');
		if(q != nil) *q = '\0';
		if(*p) elems[nelem++] = p;
		if(q == nil) break;
		p = q + 1;
	}

	/* Find device from devtab */
	d = nil;
	for(i = 0; devtab[i] != nil; i++) {
		/* Check if path element matches a device */
		if(nelem > 0 && strcmp(elems[0], "dev") == 0 && nelem > 1) {
			/* /dev/<name> — use consdevtab or similar */
			d = devtab[i];
			break;
		} else if(devtab[i]->dc == '/') {
			d = devtab[i];
			break;
		}
	}
	if(d == nil)
		error("namec: no device for path");

	c = d->attach("");
	if(c == nil)
		error("namec: attach failed");

	/* Walk remaining elements */
	if(nelem > 0) {
		Walkqid *wq = d->walk(c, nil, elems, nelem);
		if(wq == nil || wq->nqid != nelem) {
			cclose(c);
			error("namec: path not found");
		}
		c->qid = wq->qid[nelem - 1];
	}

	c = d->open(c, omode);
	return c;
}
