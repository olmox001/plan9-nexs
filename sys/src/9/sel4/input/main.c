/*
 * input/main.c — Input Protection Domain for Plan 9 on seL4 Microkit.
 *
 * Drives virtio-keyboard and virtio-tablet MMIO devices exposed by QEMU.
 * Keyboard events are encoded as Plan 9 keyboard codes and written to
 * /dev/cons in plan9_root via the shared UART ring buffer.
 * Mouse/tablet events are packed as (x, y, buttons) structs and written
 * to the input shared memory ring at SEL4_INPUT_BASE.
 *
 * This PD runs at higher priority than plan9_root; QEMU delivers IRQs
 * via virtio interrupt status register, which we poll cooperatively.
 *
 * virtio-keyboard: MMIO at 0x0a003a00 (device 29 on qemu virt aarch64)
 * virtio-tablet:   MMIO at 0x0a003c00 (device 30)
 * Input shared mem: 0x40000000 (same mapping as plan9_root devmouse)
 */

#include <u.h>
#include <libc.h>
#define fault  microkit_fault
#define strcpy microkit_strcpy
#include <microkit.h>
#undef fault
#undef strcpy

static volatile u8int thrash_buf[4 * 1024 * 1024];

static void
thrash_cache(void)
{
	volatile u8int dummy = 0;
	int i;
	for(i = 0; i < 4 * 1024 * 1024; i += 64) {
		dummy += thrash_buf[i];
	}
	(void)dummy;
}

/* ── virtio-mmio register offsets ────────────────────────────────────────── */
#define VIRTIO_MMIO_MAGIC           0x000
#define VIRTIO_MMIO_DEVICE_ID       0x008
#define VIRTIO_MMIO_DEV_FEATURES    0x010
#define VIRTIO_MMIO_DEV_FEATURES_SEL 0x014
#define VIRTIO_MMIO_DRV_FEATURES    0x020
#define VIRTIO_MMIO_DRV_FEATURES_SEL 0x024
#define VIRTIO_MMIO_QUEUE_SEL       0x030
#define VIRTIO_MMIO_QUEUE_NUM_MAX   0x034
#define VIRTIO_MMIO_QUEUE_NUM       0x038
#define VIRTIO_MMIO_QUEUE_ALIGN     0x03c
#define VIRTIO_MMIO_QUEUE_PFN       0x040
#define VIRTIO_MMIO_QUEUE_READY     0x044
#define VIRTIO_MMIO_QUEUE_NOTIFY    0x050
#define VIRTIO_MMIO_INT_STATUS      0x060
#define VIRTIO_MMIO_INT_ACK         0x064
#define VIRTIO_MMIO_STATUS          0x070
#define VIRTIO_MMIO_QUEUE_DESC_LO   0x080
#define VIRTIO_MMIO_QUEUE_DESC_HI   0x084
#define VIRTIO_MMIO_QUEUE_DRIVER_LO 0x090
#define VIRTIO_MMIO_QUEUE_DRIVER_HI 0x094
#define VIRTIO_MMIO_QUEUE_DEVICE_LO 0x0a0
#define VIRTIO_MMIO_QUEUE_DEVICE_HI 0x0a4

#define VIRTIO_S_ACKNOWLEDGE  1
#define VIRTIO_S_DRIVER       2
#define VIRTIO_S_FEATURES_OK  8
#define VIRTIO_S_DRIVER_OK    4

/* virtio-input event structure (Linux input_event subset) */
typedef struct VirtInput VirtInput;
struct VirtInput { u16int type; u16int code; s32int value; };

#define EV_SYN     0
#define EV_KEY     1
#define EV_ABS     3
#define BTN_LEFT   0x110
#define BTN_RIGHT  0x111
#define BTN_MIDDLE 0x112
#define ABS_X      0
#define ABS_Y      1

#define VRING_SIZE 64

typedef struct VRingDesc VRingDesc;
struct VRingDesc { volatile u64int addr; volatile u32int len; volatile u16int flags; volatile u16int next; };
#define VRING_DESC_F_WRITE 2

typedef struct VRingAvail VRingAvail;
struct VRingAvail { volatile u16int flags; volatile u16int idx; volatile u16int ring[VRING_SIZE]; };

typedef struct VRingUsed VRingUsed;
struct VRingUsed {
	volatile u16int flags;
	volatile u16int idx;
	struct { volatile u32int id; volatile u32int len; } ring[VRING_SIZE];
};

/* Input ring to share events with plan9_root */
#define INPUT_BASE 0x40000000ULL
typedef struct InputRing InputRing;
struct InputRing {
	volatile u32int w;
	volatile u32int r;
	volatile u8int  data[4088];
};

/* UART base for keyboard → console output */
#define UART_BASE 0x09000000ULL
#define UART_DR   ((volatile u32int*)(UART_BASE))
#define UART_FR   ((volatile u32int*)(UART_BASE + 0x18))
#define FR_TXFF   (1u << 5)

static void
uart_putc(char c)
{
	while(*UART_FR & FR_TXFF)
		;
	*UART_DR = (u32int)c;
}

/* ── Keyboard state ──────────────────────────────────────────────────────── */
static int kbd_shift;
static int kbd_ctrl;
static int kbd_alt;

static char normal_map[128] = {
	0, 0x1b, '1','2','3','4','5','6','7','8','9','0','-','=','\b','\t',
	'q','w','e','r','t','y','u','i','o','p','[',']','\n',0,
	'a','s','d','f','g','h','j','k','l',';','\'','`',0,'\\',
	'z','x','c','v','b','n','m',',','.','/',0,'*',0,' ',0,
	0,0,0,0,0,0,0,0,0,0,0,0,
	'7','8','9','-','4','5','6','+','1','2','3','0','.',
};

static char shift_map[128] = {
	0, 0x1b, '!','@','#','$','%','^','&','*','(',')','_','+','\b','\t',
	'Q','W','E','R','T','Y','U','I','O','P','{','}','\n',0,
	'A','S','D','F','G','H','J','K','L',':','"','~',0,'|',
	'Z','X','C','V','B','N','M','<','>','?',0,'*',0,' ',0,
};

static void
handle_key(u16int code, s32int value)
{
	char c;
	if(code == 42 || code == 54) { kbd_shift = value ? 1 : 0; return; }
	if(code == 29 || code == 97) { kbd_ctrl  = value ? 1 : 0; return; }
	if(code == 56)               { kbd_alt   = value ? 1 : 0; return; }

	if(value == 0) return;
	if(code >= 128) return;
	c = kbd_shift ? shift_map[code] : normal_map[code];
	if(c == 0) return;

	if(kbd_ctrl && c >= 'a' && c <= 'z')
		c -= ('a' - 1);

	uart_putc(c);
}

/* ── Mouse / tablet state ────────────────────────────────────────────────── */
static s32int mouse_x, mouse_y;
static s32int mouse_buttons;

static void
push_mouse_event(void)
{
	InputRing *ir = (InputRing*)(uintptr)INPUT_BASE;
	u8int     buf[12];
	u32int    free;
	int       i;

	buf[0] = (u8int)mouse_x;
	buf[1] = (u8int)(mouse_x >> 8);
	buf[2] = (u8int)(mouse_x >> 16);
	buf[3] = (u8int)(mouse_x >> 24);
	buf[4] = (u8int)mouse_y;
	buf[5] = (u8int)(mouse_y >> 8);
	buf[6] = (u8int)(mouse_y >> 16);
	buf[7] = (u8int)(mouse_y >> 24);
	buf[8] = (u8int)mouse_buttons;
	buf[9] = buf[10] = buf[11] = 0;

	free = sizeof(ir->data) - (ir->w - ir->r);
	if(free < 12) return;
	for(i = 0; i < 12; i++)
		ir->data[(ir->w + i) % sizeof(ir->data)] = buf[i];
	__sync_synchronize();
	ir->w += 12;
	microkit_notify(3);
}

static void
handle_mouse(u16int type, u16int code, s32int value)
{
	if(type == EV_ABS) {
		if(code == ABS_X) mouse_x = value;
		if(code == ABS_Y) mouse_y = value;
	} else if(type == EV_KEY) {
		if(code == BTN_LEFT)   mouse_buttons = value ? (mouse_buttons | 1)  : (mouse_buttons & ~1);
		if(code == BTN_MIDDLE) mouse_buttons = value ? (mouse_buttons | 2)  : (mouse_buttons & ~2);
		if(code == BTN_RIGHT)  mouse_buttons = value ? (mouse_buttons | 4)  : (mouse_buttons & ~4);
	} else if(type == EV_SYN) {
		push_mouse_event();
	}
}

#define KBD_DMA_DESC   0x7f000000ULL
#define KBD_DMA_AVAIL  0x7f000400ULL
#define KBD_DMA_USED   0x7f001000ULL
#define KBD_DMA_EVT    0x7f002000ULL

#define MS_DMA_DESC    0x7f008000ULL
#define MS_DMA_AVAIL   0x7f008400ULL
#define MS_DMA_USED    0x7f009000ULL
#define MS_DMA_EVT     0x7f00a000ULL

typedef struct VDev VDev;
struct VDev {
	volatile u32int *mmio;
	VRingDesc       *desc;
	VRingAvail      *avail;
	VRingUsed       *used;
	u8int           *evtbuf;
	u16int           last_used;
	int              is_kbd;
};

static VDev kbd_dev;
static VDev mouse_dev;

static int
vdev_init(VDev *d, uintptr base, int is_kbd)
{
	volatile u32int *m = (volatile u32int*)base;
	u32int           status = 0;
	int              i;
	uintptr          dma_base;

	d->mmio   = m;
	d->is_kbd = is_kbd;

	if(is_kbd) {
		d->desc   = (VRingDesc*)KBD_DMA_DESC;
		d->avail  = (VRingAvail*)KBD_DMA_AVAIL;
		d->used   = (VRingUsed*)KBD_DMA_USED;
		d->evtbuf = (u8int*)KBD_DMA_EVT;
		dma_base  = KBD_DMA_DESC;
	} else {
		d->desc   = (VRingDesc*)MS_DMA_DESC;
		d->avail  = (VRingAvail*)MS_DMA_AVAIL;
		d->used   = (VRingUsed*)MS_DMA_USED;
		d->evtbuf = (u8int*)MS_DMA_EVT;
		dma_base  = MS_DMA_DESC;
	}

	memset(d->desc, 0, VRING_SIZE * sizeof(VRingDesc));
	memset(d->avail, 0, sizeof(VRingAvail));
	memset(d->used, 0, sizeof(VRingUsed));
	memset(d->evtbuf, 0, VRING_SIZE * sizeof(VirtInput));

	if(m[VIRTIO_MMIO_MAGIC/4] != 0x74726976) return -1;
	if(m[VIRTIO_MMIO_DEVICE_ID/4] != 18) return -1;

	m[VIRTIO_MMIO_STATUS/4] = 0;
	status |= VIRTIO_S_ACKNOWLEDGE; m[VIRTIO_MMIO_STATUS/4] = status;
	status |= VIRTIO_S_DRIVER;      m[VIRTIO_MMIO_STATUS/4] = status;
	m[VIRTIO_MMIO_DRV_FEATURES/4] = 0;

	m[VIRTIO_MMIO_QUEUE_SEL/4] = 0;
	m[VIRTIO_MMIO_QUEUE_NUM/4] = VRING_SIZE;

	for(i = 0; i < VRING_SIZE; i++) {
		d->desc[i].addr  = (u64int)(uintptr)(d->evtbuf + i * sizeof(VirtInput));
		d->desc[i].len   = sizeof(VirtInput);
		d->desc[i].flags = VRING_DESC_F_WRITE;
		d->desc[i].next  = 0;
		d->avail->ring[i] = (u16int)i;
	}
	d->avail->idx = VRING_SIZE;
	__asm__ volatile("dsb sy; isb" ::: "memory");

	/* Configure virtqueue 0 legacy transport */
	m[VIRTIO_MMIO_QUEUE_ALIGN/4] = 4096;
	m[VIRTIO_MMIO_QUEUE_PFN/4]   = (u32int)(dma_base / 4096ULL);
	__asm__ volatile("dsb sy; isb" ::: "memory");
	thrash_cache();
	__asm__ volatile("dsb sy; isb" ::: "memory");

	/* Step 4: DRIVER_OK — device is live */
	status |= VIRTIO_S_DRIVER_OK; m[VIRTIO_MMIO_STATUS/4] = status;
	__asm__ volatile("dsb sy; isb" ::: "memory");

	return 0;
}

static void
vdev_poll(VDev *d)
{
	VirtInput  ev;
	u16int     ui;
	int        id;

	thrash_cache();
	__asm__ volatile("dsb sy; isb" ::: "memory");
	while(d->last_used != d->used->idx) {
		ui = d->last_used % VRING_SIZE;
		id = (int)d->used->ring[ui].id;
		__asm__ volatile("dsb sy; isb" ::: "memory");
		d->last_used++;

		memmove(&ev, d->evtbuf + id * sizeof(VirtInput), sizeof(VirtInput));

		if(d->is_kbd)
			handle_key(ev.code, ev.value);
		else
			handle_mouse(ev.type, ev.code, ev.value);

		d->avail->ring[d->avail->idx % VRING_SIZE] = (u16int)id;
		__asm__ volatile("dsb sy; isb" ::: "memory");
		d->avail->idx++;
		__asm__ volatile("dsb sy; isb" ::: "memory");
		thrash_cache();
		__asm__ volatile("dsb sy; isb" ::: "memory");
		d->mmio[VIRTIO_MMIO_QUEUE_NOTIFY/4] = 0;
	}
}

void
init(void)
{
	microkit_dbg_puts("input_pd: starting\n");
	memset(&kbd_dev,   0, sizeof(VDev));
	memset(&mouse_dev, 0, sizeof(VDev));
	memset((void*)0x7f000000ULL, 0, 0x10000);
	memset((void*)(uintptr)INPUT_BASE, 0, sizeof(InputRing));

	if(vdev_init(&kbd_dev,   0x0a003a00, 1) == 0)
		microkit_dbg_puts("input_pd: keyboard ok\n");
	else
		microkit_dbg_puts("input_pd: no keyboard\n");

	if(vdev_init(&mouse_dev, 0x0a003c00, 0) == 0)
		microkit_dbg_puts("input_pd: mouse ok\n");
	else
		microkit_dbg_puts("input_pd: no mouse\n");

	microkit_dbg_puts("input_pd: ready\n");
}

void
notified(microkit_channel ch)
{
	USED(ch);
	vdev_poll(&kbd_dev);
	vdev_poll(&mouse_dev);
}
