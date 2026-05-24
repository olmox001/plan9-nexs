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

	/* Set up a user stack with argv/envp */
	uintptr *tos;
	char   **argv;
	uintptr *stk;
	int      i, argc;
	char    *ustack;

	argc   = ctx->argc;
	argv   = ctx->argv;
	ustack = (char*)xalloc(USTKSIZE);
	if(ustack == nil)
		panic("exectrampoline: no stack memory");

	tos = (uintptr*)(ustack + USTKSIZE);
	tos = (uintptr*)STACKALIGN((uintptr)tos);

	/* Push envp terminator */
	*--tos = 0;
	/* Push argv strings */
	uintptr *envp_sp = tos;
	USED(envp_sp);
	/* Push argv terminator */
	*--tos = 0;
	/* Reserve space for argv pointers */
	tos -= argc;
	for(i = 0; i < argc; i++)
		tos[i] = (uintptr)argv[i];
	argv = (char**)tos;
	/* Push argc */
	*--tos = (uintptr)argc;

	/* Call the ELF entry point.  We use a C function call since we can't
	 * do an inline asm branch to a dynamic address portably from C. */
	typedef void (*EntryFn)(uintptr argc, char **argv, char **envp);
	EntryFn fn = (EntryFn)(uintptr)ctx->entry;
	fn((uintptr)argc, argv, nil);

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

	/* Open the binary */
	if(waserror())
		panic("exec9p: cannot open %s: %s", path, up->syserrstr);
	c = namec(path, Aopen, OREAD, 0);
	poperror();

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
	kstrcpy(p->text, path, sizeof(p->text));
	p->kpfun  = exectrampoline;
	p->kparg  = ctx;
	p->fgrp   = allocfgrp();
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

	if(name == nil || name[0] != '/')
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

	d->open(c, omode);
	return c;
}
