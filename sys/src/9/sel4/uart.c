#include "../port/lib.h"
#include "dat.h"
#include "fns.h"
#include "mem.h"
#include <u.h>

/*
 * PL011 UART hardware address on QEMU AArch64 'virt' board.
 * Physically located at 0x09000000, and mapped 1:1 via Microkit .system XML.
 */
#define UART_BASE 0x09000000ULL
#define UART_DR ((volatile u32int *)(UART_BASE + 0x00))
#define UART_FR ((volatile u32int *)(UART_BASE + 0x18))

/* Register status flags */
#define FR_TXFF (1u << 5) /* Transmit FIFO Full */
#define FR_RXFE (1u << 4) /* Receive FIFO Empty */

void uart_init(void) {
  /*
   * The UART is already configured and enabled by the QEMU loader/firmware,
   * so we only need to perform a simple memory barrier read to verify access.
   */
  coherence();
}

#include "font8x8.h"

static int cons_x = 0;
static int cons_y = 0;

static void fb_drawc(char c) {
  uchar *fb =
      (uchar
           *)0x30000010; /* screen pixels at SEL4_DRAW_BASE + FB_HEADER_SIZE */

  int x, y;

  if (c == '\n') {
    cons_y++;
    cons_x = 0;
  } else if (c == '\r') {
    cons_x = 0;
  } else if (c == '\b') {
    if (cons_x > 0) {
      cons_x--;
      /* Clear character to black */
      for (y = 0; y < 8; y++) {
        for (x = 0; x < 8; x++) {
          uchar *px = fb + (cons_y * 8 + y) * 4096 + (cons_x * 8 + x) * 4;
          px[0] = px[1] = px[2] = 0; /* Black */
          px[3] = 0xff;
        }
      }
    }
  } else if (c >= 32 && c <= 126) {
    unsigned char *glyph = font8x8[c - 32];

    for (y = 0; y < 8; y++) {
      unsigned char row = glyph[y];
      for (x = 0; x < 8; x++) {
        uchar *px = fb + (cons_y * 8 + y) * 4096 + (cons_x * 8 + x) * 4;

        /* Correzione: font ribaltati → invertito ordine dei bit */
        if (row & (1u << x)) {
          px[0] = px[1] = px[2] = 0xff; /* White */
        } else {
          px[0] = px[1] = px[2] = 0; /* Black */
        }
        px[3] = 0xff;
      }
    }
    cons_x++;
    if (cons_x >= 128) {
      cons_x = 0;
      cons_y++;
    }
  }

  /* Scroll screen up if we overflow the height */
  if (cons_y >= 96) {
    memmove(fb, fb + 8 * 4096, (96 - 1) * 8 * 4096);
    memset(fb + (96 - 1) * 8 * 4096, 0, 8 * 4096);
    cons_y = 95;
  }

  /* Notify display_pd to update the screen */
  plan9_microkit_notify(2);
}

static void uart_putc(char c) {
  /* Wait until Transmit FIFO is not full */
  while (*UART_FR & FR_TXFF) {
    coherence();
  }

  /* Send character */
  *UART_DR = c;

  /* Mirror character to the graphics screen */
  fb_drawc(c);

  /* Carriage return expansion */
  if (c == '\n') {
    while (*UART_FR & FR_TXFF) {
      coherence();
    }
    *UART_DR = '\r';
  }
}

void putstrn(char *str, int len) {
  for (int i = 0; i < len; i++) {
    uart_putc(str[i]);
  }
}

void uartputc(int c) { uart_putc((char)c); }

int uartgetc(void) {
  /* Return -1 if Receive FIFO is empty */
  if (*UART_FR & FR_RXFE) {
    return -1;
  }
  /* Return character */
  return (int)(*UART_DR & 0xFF);
}