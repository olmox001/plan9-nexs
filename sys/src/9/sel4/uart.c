#include <u.h>
#include "../port/lib.h"
#include "mem.h"
#include "dat.h"
#include "fns.h"

/* 
 * PL011 UART hardware address on QEMU AArch64 'virt' board.
 * Physically located at 0x09000000, and mapped 1:1 via Microkit .system XML.
 */
#define UART_BASE	0x09000000ULL
#define UART_DR		((volatile u32int*)(UART_BASE + 0x00))
#define UART_FR		((volatile u32int*)(UART_BASE + 0x18))

/* Register status flags */
#define FR_TXFF		(1u << 5)	/* Transmit FIFO Full */
#define FR_RXFE		(1u << 4)	/* Receive FIFO Empty */

void
uart_init(void)
{
	/* 
	 * The UART is already configured and enabled by the QEMU loader/firmware,
	 * so we only need to perform a simple memory barrier read to verify access.
	 */
	coherence();
}

static void
uart_putc(char c)
{
	/* Wait until Transmit FIFO is not full */
	while (*UART_FR & FR_TXFF) {
		coherence();
	}
	
	/* Send character */
	*UART_DR = c;
	
	/* Carriage return expansion */
	if (c == '\n') {
		while (*UART_FR & FR_TXFF) {
			coherence();
		}
		*UART_DR = '\r';
	}
}

void
putstrn(char *str, int len)
{
	for (int i = 0; i < len; i++) {
		uart_putc(str[i]);
	}
}

/* 
 * uartputc: required signature for some parts of Plan 9 port/ kernel console.
 */
void
uartputc(int c)
{
	uart_putc((char)c);
}

int
uartgetc(void)
{
	/* Return -1 if Receive FIFO is empty */
	if (*UART_FR & FR_RXFE) {
		return -1;
	}
	
	/* Return character */
	return (int)(*UART_DR & 0xFF);
}
