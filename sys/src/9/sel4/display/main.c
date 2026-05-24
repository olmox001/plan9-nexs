/*
 * display/main.c — Display Protection Domain for Plan 9 on seL4 Microkit.
 *
 * This PD manages the virtio-gpu device exposed by QEMU's virt machine.
 * It shares a 4MB framebuffer region with plan9_root; when plan9_root
 * sends a notification on channel SEL4_DRAW_CH (=2), this PD flushes the
 * framebuffer to the GPU scanout.
 *
 * virtio-gpu MMIO base: 0x0a003e00 (virtio device 31 on qemu virt aarch64)
 * Framebuffer shared mem: mapped at 0x30000000 (same as plan9_root)
 *
 * Build: compiled separately as display_pd.elf linked against libmicrokit.
 */

#include <u.h>
#include <libc.h>
#define fault  microkit_fault
#define strcpy microkit_strcpy
#include <microkit.h>
#undef fault
#undef strcpy

/* ── virtio-mmio register offsets (spec 1.2 §4.2.2) ─────────────────────── */
#define VIRTIO_MMIO_MAGIC           0x000
#define VIRTIO_MMIO_VERSION         0x004
#define VIRTIO_MMIO_DEVICE_ID       0x008
#define VIRTIO_MMIO_VENDOR_ID       0x00c
#define VIRTIO_MMIO_DEV_FEATURES    0x010
#define VIRTIO_MMIO_DRV_FEATURES    0x020
#define VIRTIO_MMIO_QUEUE_SEL       0x030
#define VIRTIO_MMIO_QUEUE_NUM_MAX   0x034
#define VIRTIO_MMIO_QUEUE_NUM       0x038
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
#define VIRTIO_MMIO_CONFIG_GEN      0x0fc
#define VIRTIO_MMIO_CONFIG          0x100

/* ── virtio status bits ──────────────────────────────────────────────────── */
#define VIRTIO_S_ACKNOWLEDGE  1
#define VIRTIO_S_DRIVER       2
#define VIRTIO_S_DRIVER_OK    4
#define VIRTIO_S_FEATURES_OK  8
#define VIRTIO_S_FAILED      128

/* ── virtio-gpu commands ─────────────────────────────────────────────────── */
#define VIRTIO_GPU_CMD_GET_DISPLAY_INFO      0x0100
#define VIRTIO_GPU_CMD_RESOURCE_CREATE_2D    0x0101
#define VIRTIO_GPU_CMD_RESOURCE_UNREF        0x0102
#define VIRTIO_GPU_CMD_SET_SCANOUT           0x0103
#define VIRTIO_GPU_CMD_RESOURCE_FLUSH        0x0104
#define VIRTIO_GPU_CMD_TRANSFER_TO_HOST_2D   0x0105
#define VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING 0x0106

#define VIRTIO_GPU_RESP_OK_NODATA            0x1100
#define VIRTIO_GPU_RESP_OK_DISPLAY_INFO      0x1101

#define VIRTIO_GPU_FORMAT_B8G8R8A8_UNORM     1

/* ── vring structures (spec 2.7) ─────────────────────────────────────────── */
#define VRING_QUEUE_SIZE 16

typedef struct VRingDesc VRingDesc;
struct VRingDesc {
	u64int addr;
	u32int len;
	u16int flags;
	u16int next;
};

#define VRING_DESC_F_WRITE 2

typedef struct VRingAvail VRingAvail;
struct VRingAvail {
	u16int flags;
	u16int idx;
	u16int ring[VRING_QUEUE_SIZE];
};

typedef struct VRingUsedElem VRingUsedElem;
struct VRingUsedElem { u32int id; u32int len; };

typedef struct VRingUsed VRingUsed;
struct VRingUsed {
	u16int flags;
	u16int idx;
	VRingUsedElem ring[VRING_QUEUE_SIZE];
};

/* ── GPU header (all commands/responses start with this) ─────────────────── */
typedef struct GpuHdr GpuHdr;
struct GpuHdr { u32int type; u32int flags; u64int fence_id; u32int ctx_id; u32int padding; };

typedef struct GpuRect GpuRect;
struct GpuRect { u32int x; u32int y; u32int width; u32int height; };

typedef struct GpuCreate2D GpuCreate2D;
struct GpuCreate2D { GpuHdr hdr; u32int resource_id; u32int format; u32int width; u32int height; };

typedef struct GpuSetScanout GpuSetScanout;
struct GpuSetScanout { GpuHdr hdr; GpuRect r; u32int scanout_id; u32int resource_id; };

typedef struct GpuFlush GpuFlush;
struct GpuFlush { GpuHdr hdr; GpuRect r; u32int resource_id; u32int padding; };

typedef struct GpuTransfer GpuTransfer;
struct GpuTransfer { GpuHdr hdr; GpuRect r; u64int offset; u32int resource_id; u32int padding; };

typedef struct GpuAttachBacking GpuAttachBacking;
struct GpuAttachBacking {
	GpuHdr  hdr;
	u32int  resource_id;
	u32int  nr_entries;
	u64int  addr;
	u32int  length;
	u32int  padding;
};

/* ── Hardware base addresses ─────────────────────────────────────────────── */
#define VIRTIO_GPU_MMIO   0x0a003e00ULL
#define FB_VADDR          0x30000000ULL
#define FB_HEADER_SIZE    16   /* FBHeader: w, h, depth, stride */
#define DRAW_WIDTH        1024
#define DRAW_HEIGHT       768
#define DRAW_STRIDE       (DRAW_WIDTH * 4)
#define DRAW_FBSIZE       (DRAW_HEIGHT * DRAW_STRIDE)
#define RESOURCE_ID       1

static volatile u32int *gpu;

static u32int r32(u32int off) { return gpu[off/4]; }
static void   w32(u32int off, u32int v) { gpu[off/4] = v; }

/* DMA-capable buffers (allocated in our own memory space) */
static VRingDesc  desc[VRING_QUEUE_SIZE]   __attribute__((aligned(4096)));
static VRingAvail avail                    __attribute__((aligned(2)));
static VRingUsed  used                     __attribute__((aligned(4096)));

/* Command/response buffers */
static u8int      cmdbuf[512]              __attribute__((aligned(64)));
static u8int      rspbuf[512]              __attribute__((aligned(64)));

static u16int desc_head;

static void
gpu_cmd(void *cmd, u32int cmdlen, void *rsp, u32int rsplen)
{
	u16int d0, d1, ai;

	d0 = desc_head % VRING_QUEUE_SIZE;
	d1 = (desc_head + 1) % VRING_QUEUE_SIZE;
	desc_head += 2;

	memmove(cmdbuf, cmd, cmdlen);

	desc[d0].addr  = (u64int)(uintptr)cmdbuf;
	desc[d0].len   = cmdlen;
	desc[d0].flags = 1;   /* NEXT */
	desc[d0].next  = d1;

	desc[d1].addr  = (u64int)(uintptr)rspbuf;
	desc[d1].len   = rsplen;
	desc[d1].flags = VRING_DESC_F_WRITE;
	desc[d1].next  = 0;

	ai = avail.idx % VRING_QUEUE_SIZE;
	avail.ring[ai] = d0;
	__sync_synchronize();
	avail.idx++;
	__sync_synchronize();

	w32(VIRTIO_MMIO_QUEUE_NOTIFY, 0);

	/* Poll until device processes it */
	while(used.idx != avail.idx)
		__asm__ volatile("" ::: "memory");

	if(rsp)
		memmove(rsp, rspbuf, rsplen);
}

static void
gpu_init(void)
{
	u32int status = 0;

	gpu = (volatile u32int*)(uintptr)VIRTIO_GPU_MMIO;

	if(r32(VIRTIO_MMIO_MAGIC) != 0x74726976) {
		microkit_dbg_puts("display_pd: no virtio-gpu found\n");
		return;
	}
	if(r32(VIRTIO_MMIO_DEVICE_ID) != 16) {
		microkit_dbg_puts("display_pd: not a GPU device\n");
		return;
	}

	/* Reset */
	w32(VIRTIO_MMIO_STATUS, 0);
	status |= VIRTIO_S_ACKNOWLEDGE; w32(VIRTIO_MMIO_STATUS, status);
	status |= VIRTIO_S_DRIVER;      w32(VIRTIO_MMIO_STATUS, status);
	/* Accept all features */
	w32(VIRTIO_MMIO_DEV_FEATURES, 0);
	w32(VIRTIO_MMIO_DRV_FEATURES, 0);
	status |= VIRTIO_S_FEATURES_OK; w32(VIRTIO_MMIO_STATUS, status);
	status |= VIRTIO_S_DRIVER_OK;   w32(VIRTIO_MMIO_STATUS, status);

	/* Set up virtqueue 0 */
	w32(VIRTIO_MMIO_QUEUE_SEL, 0);
	u32int qmax = r32(VIRTIO_MMIO_QUEUE_NUM_MAX);
	if(qmax < VRING_QUEUE_SIZE) {
		microkit_dbg_puts("display_pd: queue too small\n");
		return;
	}
	w32(VIRTIO_MMIO_QUEUE_NUM, VRING_QUEUE_SIZE);
	w32(VIRTIO_MMIO_QUEUE_DESC_LO,   (u32int)(uintptr)desc);
	w32(VIRTIO_MMIO_QUEUE_DESC_HI,   (u32int)((uintptr)desc >> 32));
	w32(VIRTIO_MMIO_QUEUE_DRIVER_LO, (u32int)(uintptr)&avail);
	w32(VIRTIO_MMIO_QUEUE_DRIVER_HI, (u32int)((uintptr)&avail >> 32));
	w32(VIRTIO_MMIO_QUEUE_DEVICE_LO, (u32int)(uintptr)&used);
	w32(VIRTIO_MMIO_QUEUE_DEVICE_HI, (u32int)((uintptr)&used >> 32));
	w32(VIRTIO_MMIO_QUEUE_READY, 1);

	microkit_dbg_puts("display_pd: virtio-gpu initialized\n");
}

static void
gpu_create_fb(void)
{
	GpuCreate2D    cmd;
	GpuHdr         rsp;
	GpuAttachBacking ab;
	GpuSetScanout  ss;

	/* Create 2D resource */
	memset(&cmd, 0, sizeof cmd);
	cmd.hdr.type    = VIRTIO_GPU_CMD_RESOURCE_CREATE_2D;
	cmd.resource_id = RESOURCE_ID;
	cmd.format      = VIRTIO_GPU_FORMAT_B8G8R8A8_UNORM;
	cmd.width       = DRAW_WIDTH;
	cmd.height      = DRAW_HEIGHT;
	gpu_cmd(&cmd, sizeof cmd, &rsp, sizeof rsp);

	/* Attach backing: point GPU at our framebuffer pixels */
	memset(&ab, 0, sizeof ab);
	ab.hdr.type    = VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING;
	ab.resource_id = RESOURCE_ID;
	ab.nr_entries  = 1;
	ab.addr        = (u64int)(FB_VADDR + FB_HEADER_SIZE);
	ab.length      = DRAW_FBSIZE;
	gpu_cmd(&ab, sizeof ab, &rsp, sizeof rsp);

	/* Set scanout to this resource */
	memset(&ss, 0, sizeof ss);
	ss.hdr.type    = VIRTIO_GPU_CMD_SET_SCANOUT;
	ss.r.x = 0; ss.r.y = 0; ss.r.width = DRAW_WIDTH; ss.r.height = DRAW_HEIGHT;
	ss.scanout_id  = 0;
	ss.resource_id = RESOURCE_ID;
	gpu_cmd(&ss, sizeof ss, &rsp, sizeof rsp);

	microkit_dbg_puts("display_pd: framebuffer attached\n");
}

/* Flush the entire framebuffer to the GPU scanout */
static void
gpu_flush(void)
{
	GpuTransfer tr;
	GpuFlush    fl;
	GpuHdr      rsp;

	/* Transfer host → device */
	memset(&tr, 0, sizeof tr);
	tr.hdr.type    = VIRTIO_GPU_CMD_TRANSFER_TO_HOST_2D;
	tr.r.x = 0; tr.r.y = 0; tr.r.width = DRAW_WIDTH; tr.r.height = DRAW_HEIGHT;
	tr.offset      = 0;
	tr.resource_id = RESOURCE_ID;
	gpu_cmd(&tr, sizeof tr, &rsp, sizeof rsp);

	/* Flush scanout */
	memset(&fl, 0, sizeof fl);
	fl.hdr.type    = VIRTIO_GPU_CMD_RESOURCE_FLUSH;
	fl.r.x = 0; fl.r.y = 0; fl.r.width = DRAW_WIDTH; fl.r.height = DRAW_HEIGHT;
	fl.resource_id = RESOURCE_ID;
	gpu_cmd(&fl, sizeof fl, &rsp, sizeof rsp);
}

void
init(void)
{
	microkit_dbg_puts("display_pd: starting\n");
	gpu_init();
	gpu_create_fb();
	microkit_dbg_puts("display_pd: ready\n");
}

void
notified(microkit_channel ch)
{
	if(ch == 2)   /* SEL4_DRAW_CH from plan9_root */
		gpu_flush();
}
