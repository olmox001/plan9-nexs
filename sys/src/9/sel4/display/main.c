/*
 * display/main.c — Display Protection Domain for Plan 9 on seL4 Microkit.
 *
 * This PD manages the PCIe standard VGA card (Bochs VGA with legacy emulation)
 * emulated by QEMU. It shares a 4MB framebuffer region with plan9_root;
 * when plan9_root sends a notification on channel SEL4_DRAW_CH (=2), this PD
 * flushes the framebuffer directly to the VGA linear framebuffer mapped on PCI.
 *
 * PCI physical address mappings:
 *   PCI ECAM (Config Space): phys 0x3f000000, mapped at vaddr 0x3f000000 (1MB, uncached)
 *   VGA Framebuffer BAR 0:    phys 0x38000000, mapped at vaddr 0x38000000 (16MB, uncached)
 *   VGA VBE Registers BAR 2:  phys 0x3c000000, mapped at vaddr 0x3c000000 (4KB, uncached)
 *
 * Shared memory:
 *   Plan 9 Framebuffer:       phys 0x7d000000, mapped at vaddr 0x7d000000 (4MB, cached)
 */

#include <u.h>
#include <libc.h>
#define fault  microkit_fault
#define strcpy microkit_strcpy
#include <microkit.h>
#undef fault
#undef strcpy

/* ── Bochs VBE (Dispi) register indices (shifted left by 1 for word access) ── */
#define VBE_DISPI_INDEX_ID          0x0
#define VBE_DISPI_INDEX_XRES        0x1
#define VBE_DISPI_INDEX_YRES        0x2
#define VBE_DISPI_INDEX_BPP         0x3
#define VBE_DISPI_INDEX_ENABLE      0x4
#define VBE_DISPI_INDEX_BANK        0x5
#define VBE_DISPI_INDEX_VIRT_WIDTH  0x6
#define VBE_DISPI_INDEX_VIRT_HEIGHT 0x7
#define VBE_DISPI_INDEX_X_OFFSET    0x8
#define VBE_DISPI_INDEX_Y_OFFSET    0x9

#define VBE_DISPI_DISABLED          0x00
#define VBE_DISPI_ENABLED           0x01
#define VBE_DISPI_LFB_ENABLED       0x40

/* ── Display dimensions (1024x768x32bpp) ────────────────────────────────── */
#define DRAW_WIDTH        1024
#define DRAW_HEIGHT       768
#define DRAW_DEPTH        32
#define DRAW_STRIDE       (DRAW_WIDTH * 4)
#define DRAW_FBSIZE       (DRAW_HEIGHT * DRAW_STRIDE)

#define FB_PHYS           0x7d000000ULL
#define FB_HEADER_SIZE    16

static volatile u32int *pci_ecam;
static volatile u16int *vbe;
static volatile u32int *vga_fb;
static int             gpu_ok;

static void
uart_puts(const char *s)
{
	volatile u32int *uart = (volatile u32int*)0x09000000;
	while(*s) {
		while(uart[6] & (1u << 5)) /* FR.TXFF */
			;
		uart[0] = (u32int)(unsigned char)*s++;
	}
}

static void
pci_init(void)
{
	pci_ecam = (volatile u32int*)0x3f000000;
	int dev;

	/* Scan Bus 0, slots 1..31 to find the QEMU Standard VGA Card (Vendor 1234, Device 1111) */
	for(dev = 1; dev < 32; dev++) {
		volatile u32int *dev_cfg = (volatile u32int*)((uintptr)pci_ecam + (dev << 15));
		u32int id = dev_cfg[0];
		if((id & 0xffff) == 0x1234) {
			/* Found the standard PCI VGA Card! */
			/* 1. Configure BAR 0 to physical 0x38000000 */
			dev_cfg[4] = 0x38000000;
			/* 2. Configure BAR 2 to physical 0x3c000000 */
			dev_cfg[6] = 0x3c000000;
			/* 3. Enable Memory Space access + Bus Master in Command register (offset 0x04) */
			u32int cmd_status = dev_cfg[1];
			dev_cfg[1] = (cmd_status & 0xffff0000) | 0x0006;
			
			__asm__ volatile("dsb sy; isb" ::: "memory");

			char dbg[256];
			snprint(dbg, sizeof dbg, "display_pd: found QEMU PCI VGA on dev %d, mapped BAR0=0x38000000 BAR2=0x3c000000\n", dev);
			uart_puts(dbg);

			/* Read back BARs and Command to verify configuration succeeded */
			u32int read_bar0 = dev_cfg[4];
			u32int read_bar2 = dev_cfg[6];
			u32int read_cmd = dev_cfg[1];
			snprint(dbg, sizeof dbg, "display_pd DIAG PCI: BAR0_read=0x%x BAR2_read=0x%x CMD_read=0x%x\n", read_bar0, read_bar2, read_cmd);
			uart_puts(dbg);
			return;
		}
	}
	uart_puts("display_pd: ERROR: QEMU VGA card not found on PCIe bus!\n");
}

static void
vga_unblank(void)
{
	/* Remapped standard VGA registers start at BAR 2 + 0x400 */
	volatile u8int *vga = (volatile u8int*)0x3c000400;

	/* Reset flip-flop of Attribute Controller by reading Input Status Register 1 at offset 0x1a (0x3da) */
	volatile u8int *isr1 = (volatile u8int*)(0x3c000400 + 0x1a);
	(void)*isr1;

	/* Write 0x20 to Attribute Controller Index (offset 0x00 = 0x3c0) to unblank the screen (bit 5 = 1) */
	vga[0] = 0x20;

	/* Unblank Sequencer: Sequencer Index is at offset 0x04 (0x3c4), Data at 0x05 (0x3c5).
	   We want to set Index 1 (Clocking Mode) bit 5 (Screen Off) to 0. */
	vga[4] = 0x01;
	u8int clk_mode = vga[5];
	vga[5] = clk_mode & ~0x20;

	__asm__ volatile("dsb sy; isb" ::: "memory");
	uart_puts("display_pd: Sequencer and Attribute Controller unblanked\n");
}

static void
vbe_write(u32int reg, u16int val)
{
	/* VBE registers are accessed via BAR 2 at offset 0x500 + (reg << 1) */
	vbe[reg] = val;
}

static void
gpu_init(void)
{
	pci_init();
	vga_unblank();
	
	vbe = (volatile u16int*)0x3c000500;
	vga_fb = (volatile u32int*)0x38000000;

	/* Configure VESA VBE resolution via MMIO registers */
	vbe_write(VBE_DISPI_INDEX_ENABLE, VBE_DISPI_DISABLED);
	vbe_write(VBE_DISPI_INDEX_XRES, DRAW_WIDTH);
	vbe_write(VBE_DISPI_INDEX_YRES, DRAW_HEIGHT);
	vbe_write(VBE_DISPI_INDEX_BPP, DRAW_DEPTH);
	vbe_write(VBE_DISPI_INDEX_VIRT_WIDTH, DRAW_WIDTH);
	vbe_write(VBE_DISPI_INDEX_VIRT_HEIGHT, DRAW_HEIGHT);
	vbe_write(VBE_DISPI_INDEX_BANK, 0);
	vbe_write(VBE_DISPI_INDEX_X_OFFSET, 0);
	vbe_write(VBE_DISPI_INDEX_Y_OFFSET, 0);
	vbe_write(VBE_DISPI_INDEX_ENABLE, VBE_DISPI_ENABLED | VBE_DISPI_LFB_ENABLED);

	__asm__ volatile("dsb sy; isb" ::: "memory");

	gpu_ok = 1;
	microkit_dbg_puts("display_pd: standard VGA initialized via VBE\n");

	/* Read back VBE registers to verify they were configured correctly */
	char dbg[256];
	u16int vbe_id = vbe[VBE_DISPI_INDEX_ID];
	u16int vbe_enable = vbe[VBE_DISPI_INDEX_ENABLE];
	u16int vbe_xres = vbe[VBE_DISPI_INDEX_XRES];
	u16int vbe_yres = vbe[VBE_DISPI_INDEX_YRES];
	u16int vbe_bpp = vbe[VBE_DISPI_INDEX_BPP];
	snprint(dbg, sizeof dbg, "display_pd DIAG VBE: VBE_ID=0x%x ENABLE=0x%x XRES=%d YRES=%d BPP=%d\n", vbe_id, vbe_enable, vbe_xres, vbe_yres, vbe_bpp);
	uart_puts(dbg);
}

static void
gpu_create_fb(void)
{
	/* No extra step required for flat VGA LFB */
	microkit_dbg_puts("display_pd: framebuffer is ready\n");
}

static void
gpu_flush(void)
{
	/* Copy the shared framebuffer BGRA pixels directly to the flat VGA LFB BAR 0 */
	memmove((void*)vga_fb, (void*)(FB_PHYS + FB_HEADER_SIZE), DRAW_FBSIZE);
	__asm__ volatile("dsb sy; isb" ::: "memory");
}

void
init(void)
{
	microkit_dbg_puts("display_pd: starting VGA mode\n");
	gpu_init();
	if(gpu_ok) {
		gpu_create_fb();
		gpu_flush();
	}
	microkit_dbg_puts("display_pd: ready\n");
}

void
notified(microkit_channel ch)
{
	if(ch == 2 && gpu_ok)
		gpu_flush();
}
