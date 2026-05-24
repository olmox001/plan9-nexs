#include <u.h>
#include <libc.h>

/*
 * setlabel and gotolabel for GCC on AArch64 (ARM64).
 * Under GCC's procedure call standard, x19-x29 are callee-saved registers.
 * We must save/restore them all to prevent register corruption during
 * cooperative thread switches.
 */

__attribute__((naked)) int
setlabel(Label *l)
{
	__asm__ __volatile__(
		"stp x19, x20, [x0, #0]\n"
		"stp x21, x22, [x0, #16]\n"
		"stp x23, x24, [x0, #32]\n"
		"stp x25, x26, [x0, #48]\n"
		"stp x27, x28, [x0, #64]\n"
		"stp x29, x30, [x0, #80]\n"
		"mov x1, sp\n"
		"str x1, [x0, #96]\n"
		"mov w0, #0\n"
		"ret\n"
	);
}

__attribute__((naked)) void
gotolabel(Label *l)
{
	__asm__ __volatile__(
		"ldp x19, x20, [x0, #0]\n"
		"ldp x21, x22, [x0, #16]\n"
		"ldp x23, x24, [x0, #32]\n"
		"ldp x25, x26, [x0, #48]\n"
		"ldp x27, x28, [x0, #64]\n"
		"ldp x29, x30, [x0, #80]\n"
		"ldr x1, [x0, #96]\n"
		"mov sp, x1\n"
		"mov w0, #1\n"
		"ret\n"
	);
}

/*
 * setjmp and longjmp (userspace standard C equivalents).
 * They behave identically to setlabel/gotolabel, but save/restore
 * into jmp_buf, and longjmp allows passing a specific return value.
 */

__attribute__((naked)) int
setjmp(jmp_buf env)
{
	__asm__ __volatile__(
		"stp x19, x20, [x0, #0]\n"
		"stp x21, x22, [x0, #16]\n"
		"stp x23, x24, [x0, #32]\n"
		"stp x25, x26, [x0, #48]\n"
		"stp x27, x28, [x0, #64]\n"
		"stp x29, x30, [x0, #80]\n"
		"mov x1, sp\n"
		"str x1, [x0, #96]\n"
		"mov w0, #0\n"
		"ret\n"
	);
}

__attribute__((naked)) void
longjmp(jmp_buf env, int val)
{
	__asm__ __volatile__(
		"ldp x19, x20, [x0, #0]\n"
		"ldp x21, x22, [x0, #16]\n"
		"ldp x23, x24, [x0, #32]\n"
		"ldp x25, x26, [x0, #48]\n"
		"ldp x27, x28, [x0, #64]\n"
		"ldp x29, x30, [x0, #80]\n"
		"ldr x1, [x0, #96]\n"
		"mov sp, x1\n"
		"mov w0, w1\n"
		"cbnz w0, 1f\n"
		"mov w0, #1\n"
		"1:\n"
		"ret\n"
	);
}
