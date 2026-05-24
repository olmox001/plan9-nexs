#ifndef _SEL4_MEM_H_
#define _SEL4_MEM_H_

#define KiB		1024u
#define MiB		1048576u
#define GiB		1073741824u

/* Page size: 4KiB */
#define	PGSHIFT		12		/* log(BY2PG) */
#define	BY2PG		(1ULL<<PGSHIFT)	/* bytes per page */
#define	ROUND(s, sz)	(((s)+(sz-1))&~(sz-1))
#define	PGROUND(s)	ROUND(s, BY2PG)

#define	MAXMACH		1			/* single core bootstrap for now */
#define	MACHSIZE	(8*KiB)
#define KSTACK		(8*KiB)
#define STACKALIGN(sp)	((sp) & ~15)		/* AArch64 ABI 16-byte alignment */

#define	KZERO		0x00000000ULL		/* simple direct offset */
#define	UZERO		0ULL
#define	UTZERO		0x10000ULL
#define	USTKTOP		0x40000000ULL		/* user space bounds */
#define	USTKSIZE	(16*MiB)

#define BLOCKALIGN	64

#define BI2BY		8
#define BY2SE		4
#define BY2WD		8
#define BY2V		8

#define MIN(a, b)	((a) < (b)? (a): (b))
#define MAX(a, b)	((a) > (b)? (a): (b))

/* MMU Page table sizes required by portdat.h */
#define	PTEMAPMEM	(1024*1024)
#define	PTEPERTAB	(PTEMAPMEM/BY2PG)
#define	SSEGMAPSIZE	16

#endif /* _SEL4_MEM_H_ */
