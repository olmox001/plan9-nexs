#ifndef _SEL4_U_H_
#define _SEL4_U_H_

#define nil		((void*)0)

typedef	unsigned short	ushort;
typedef	unsigned char	uchar;
typedef	unsigned int	uint;
typedef	signed char	schar;

/* 
 * GCC LP64 Model (aarch64, x86_64):
 * int is 32-bit, long is 64-bit, long long is 64-bit.
 * Plan 9 expects:
 * ulong/long to be 32-bit, and vlong/uvlong to be 64-bit.
 * So we define ulong as unsigned int (32-bit).
 */
typedef	unsigned int	ulong;
typedef	long long	vlong;
typedef	unsigned long long uvlong;

typedef vlong		intptr;
typedef uvlong		uintptr;
typedef uvlong		usize;
typedef	uint		Rune;

typedef union FPdbleword FPdbleword;
union FPdbleword
{
	double	x;
	struct {	/* little endian */
		uint lo;
		uint hi;
	};
};

/* 
 * Use GCC's standard builtins for robust varargs, 
 * completely bypassing the custom pointer arithmetic va_list of 7c/6c.
 */
typedef	__builtin_va_list	va_list;
#define va_start(ap, last)	__builtin_va_start(ap, last)
#define va_arg(ap, type)	__builtin_va_arg(ap, type)
#define va_end(ap)		__builtin_va_end(ap)
#define va_copy(dest, src)	__builtin_va_copy(dest, src)

/* 
 * Extended jmp_buf for cooperative green thread context switching under GCC.
 * Saves GCC's callee-saved registers x19-x29, lr (PC), and sp.
 * We use 16 words to remain 16-byte aligned.
 */
typedef uintptr	jmp_buf[16];
#define	JMPBUFSP	12
#define	JMPBUFPC	11
#define	JMPBUFDPC	0

typedef struct Label Label;
struct Label {
	uintptr regs[16];
};

/* Multi-precision digit type */
typedef unsigned int	mpdigit;

/* Standard Plan 9 u8int/u16int types */
typedef unsigned char u8int;
typedef unsigned short u16int;
typedef unsigned int	u32int;
typedef unsigned long long u64int;
typedef signed char s8int;
typedef signed short s16int;
typedef signed int s32int;
typedef signed long long s64int;

#endif /* _SEL4_U_H_ */
