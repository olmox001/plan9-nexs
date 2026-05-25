# Makefile for Plan 9 (9front) su seL4 Microkit (AArch64)
# ==========================================================

# Target configuration
BUILD_DIR ?= $(abspath .)
MICROKIT_SDK ?= /Users/olmo/Documents/git/nexskernel/root/release/microkit-sdk-2.2.0-dev
MICROKIT_BOARD ?= qemu_virt_aarch64
MICROKIT_CONFIG ?= debug
LLVM ?= True

# Toolchain selection based on LLVM flag
ifeq ($(LLVM),True)
    CC := clang -target aarch64-none-elf
    AR := llvm-ar
    LD := ld.lld
else
    CC := aarch64-none-elf-gcc
    AR := aarch64-none-elf-ar
    LD := aarch64-none-elf-ld
endif

# Board directory
BOARD_DIR := $(MICROKIT_SDK)/board/$(MICROKIT_BOARD)/$(MICROKIT_CONFIG)

# Compiler flags
# We pass -fplan9-extensions to support Plan 9 anonymous structs natively in GCC/Clang!
CFLAGS_ARCH := -mstrict-align -march=armv8-a
INCS := -I$(BUILD_DIR) -I../../../../sel4/include -I../../../include -I. -I./include -I../../libmp/port -I$(BOARD_DIR)/include
CFLAGS := -O2 -g -Wall -Wextra -Wno-unknown-pragmas -Wno-unused-parameter \
          -Wno-unused-function -Wno-incompatible-pointer-types -ffreestanding \
          -fms-extensions -nostdlib -fcommon -include plan9_compat.h \
          $(CFLAGS_ARCH) $(INCS)

# Main output target
TARGET_IMAGE := $(BUILD_DIR)/loader.img
TARGET_ELF   := $(BUILD_DIR)/plan9_root.elf
SERVER_ELF   := $(BUILD_DIR)/9pserver.elf
REPORT_FILE  := $(BUILD_DIR)/report.txt

# Library output files
LIBC_A     := $(BUILD_DIR)/libc_sel4.a
LIBMP_A    := $(BUILD_DIR)/libmp_sel4.a
LIBSEC_A   := $(BUILD_DIR)/libsec_sel4.a
LIBTHREAD_A := $(BUILD_DIR)/libthread_sel4.a
LIBBIO_A   := $(BUILD_DIR)/libbio_sel4.a

# User binary output files
ECHO_ELF   := $(BUILD_DIR)/echo.elf
CAT_ELF    := $(BUILD_DIR)/cat.elf
LS_ELF     := $(BUILD_DIR)/ls.elf
PWD_ELF    := $(BUILD_DIR)/pwd.elf
MKDIR_ELF  := $(BUILD_DIR)/mkdir.elf
RM_ELF     := $(BUILD_DIR)/rm.elf
RC_ELF     := $(BUILD_DIR)/rc.elf

USER_BINS  := $(ECHO_ELF) $(CAT_ELF) $(LS_ELF) $(PWD_ELF) $(MKDIR_ELF) $(RM_ELF) $(RC_ELF)

# ── Source File Definitions ──────────────────────────────────────────────────

# libc/sel4/ custom files
LIBC_SEL4_SRCS := \
    ../../libc/sel4/tas.c \
    ../../libc/sel4/atom.c \
    ../../libc/sel4/getcallerpc.c \
    ../../libc/sel4/setlabel.c \
    ../../libc/sel4/fmtstubs.c \
    ../../libc/sel4/main9.c \
    ../../libc/sel4/syscall.c \
    ../../libc/sel4/lock.c

# libc/port/ standard string/utility files
LIBC_PORT_SRCS := \
    ../../libc/port/strlen.c \
    ../../libc/port/strcmp.c \
    ../../libc/port/strcpy.c \
    ../../libc/port/strncpy.c \
    ../../libc/port/strcat.c \
    ../../libc/port/strncat.c \
    ../../libc/port/strchr.c \
    ../../libc/port/strrchr.c \
    ../../libc/port/strstr.c \
    ../../libc/port/memmove.c \
    ../../libc/port/memset.c \
    ../../libc/port/memcmp.c \
    ../../libc/port/memchr.c \
    ../../libc/port/strncmp.c \
    ../../libc/port/strtol.c \
    ../../libc/port/strtoul.c \
    ../../libc/port/strtoll.c \
    ../../libc/port/strtoull.c \
    ../../libc/port/qsort.c \
    ../../libc/port/rune.c \
    ../../libc/port/runestrlen.c \
    ../../libc/port/runestrcmp.c \
    ../../libc/port/runestrcpy.c \
    ../../libc/port/memccpy.c \
    ../../libc/port/utflen.c \
    ../../libc/port/malloc.c \
    ../../libc/port/pool.c \
    ../../libc/port/atol.c \
    ../../libc/port/utfrrune.c \
    ../../libc/port/atexit.c \
    ../../libc/port/quote.c \
    ../../libc/port/exits.c \
    ../../libc/port/strdup.c \
    ../../libc/port/runestrdup.c \
    ../../libc/port/needsrcquote.c \
    ../../libc/port/cistrncmp.c \
    ../../libc/port/ctype.c \
    ../../libc/port/strspn.c \
    ../../libc/port/runestrchr.c \
    ../../libc/port/utfrune.c \
    ../../libc/port/tokenize.c \
    ../../libc/port/_assert.c

# libc/fmt/ Plan 9 print engine
LIBC_FMT_SRCS := \
    ../../libc/fmt/fmt.c \
    ../../libc/fmt/fmtprint.c \
    ../../libc/fmt/fprint.c \
    ../../libc/fmt/print.c \
    ../../libc/fmt/sprint.c \
    ../../libc/fmt/snprint.c \
    ../../libc/fmt/vsnprint.c \
    ../../libc/fmt/smprint.c \
    ../../libc/fmt/vsmprint.c \
    ../../libc/fmt/dofmt.c \
    ../../libc/fmt/fmtstr.c \
    ../../libc/fmt/fmtvprint.c \
    ../../libc/fmt/seprint.c \
    ../../libc/fmt/vseprint.c \
    ../../libc/fmt/vfprint.c \
    ../../libc/fmt/fmtfd.c \
    ../../libc/fmt/fmtquote.c \
    ../../libc/fmt/fmtrune.c

# Note: fltfmt.c (float formatting) and fmtlock.c (lock/unlock) and errfmt.c
# (%r verb) are intentionally excluded; fmtstubs.c provides no-op replacements.

# libc/9sys/ Plan 9 serialization/system functions
LIBC_9SYS_SRCS := \
    ../../libc/9sys/sysfatal.c \
    ../../libc/9sys/dirread.c \
    ../../libc/9sys/nulldir.c \
    ../../libc/9sys/convD2M.c \
    ../../libc/9sys/convM2D.c \
    ../../libc/9sys/convM2S.c \
    ../../libc/9sys/convS2M.c \
    ../../libc/9sys/fcallfmt.c \
    ../../libc/9sys/abort.c \
    ../../libc/9sys/werrstr.c \
    ../../libc/9sys/access.c \
    ../../libc/9sys/wait.c \
    ../../libc/9sys/dirstat.c \
    ../../libc/9sys/getwd.c \
    ../../libc/9sys/ctime.c \
    ../../libc/9sys/time.c \
    ../../libc/9sys/dirmodefmt.c \
    ../../libc/9sys/qlock.c

# Compile all C library files into a single list of objects
LIBC_OBJS := $(patsubst %.c,$(BUILD_DIR)/libc/%.o,$(notdir $(LIBC_SEL4_SRCS) $(LIBC_PORT_SRCS) $(LIBC_FMT_SRCS) $(LIBC_9SYS_SRCS))) \
             $(BUILD_DIR)/libc/date.o

# libmp/port/ Multi-Precision math C files
LIBMP_SRCS := \
    ../../libmp/port/mpaux.c \
    ../../libmp/port/mpadd.c \
    ../../libmp/port/mpsub.c \
    ../../libmp/port/mpmul.c \
    ../../libmp/port/mpdiv.c \
    ../../libmp/port/mpmod.c \
    ../../libmp/port/mpexp.c \
    ../../libmp/port/mpfield.c

LIBMP_OBJS := $(patsubst %.c,$(BUILD_DIR)/libmp/%.o,$(notdir $(LIBMP_SRCS)))

# libsec/port/ Crypto C files
LIBSEC_SRCS := \
    ../../libsec/port/md5.c \
    ../../libsec/port/md5block.c \
    ../../libsec/port/sha1.c \
    ../../libsec/port/sha1block.c \
    ../../libsec/port/sha2_64.c \
    ../../libsec/port/sha2block64.c \
    ../../libsec/port/aes.c

LIBSEC_OBJS := $(patsubst %.c,$(BUILD_DIR)/libsec/%.o,$(notdir $(LIBSEC_SRCS)))

# libthread/ Thread engine files (plus custom sel4.c)
LIBTHREAD_SRCS := \
    ../../libthread/thread.c \
    ../../libthread/channel.c \
    ../../libthread/create.c \
    ../../libthread/debug.c \
    ../../libthread/exit.c \
    ../../libthread/id.c \
    ../../libthread/lib.c \
    ../../libthread/main.c \
    ../../libthread/sched.c \
    ../../libthread/sel4.c

LIBTHREAD_OBJS := $(patsubst %.c,$(BUILD_DIR)/libthread/%.o,$(notdir $(LIBTHREAD_SRCS)))

# libbio/ Bio buffering library C files
LIBBIO_SRCS := \
    ../../libbio/bbuffered.c \
    ../../libbio/bfildes.c \
    ../../libbio/bflush.c \
    ../../libbio/bgetrune.c \
    ../../libbio/bgetc.c \
    ../../libbio/bgetd.c \
    ../../libbio/binit.c \
    ../../libbio/blethal.c \
    ../../libbio/boffset.c \
    ../../libbio/bprint.c \
    ../../libbio/bputrune.c \
    ../../libbio/bputc.c \
    ../../libbio/brdline.c \
    ../../libbio/brdstr.c \
    ../../libbio/bread.c \
    ../../libbio/bseek.c \
    ../../libbio/bwrite.c \
    ../../libbio/bvprint.c

LIBBIO_OBJS := $(patsubst %.c,$(BUILD_DIR)/libbio/%.o,$(notdir $(LIBBIO_SRCS)))

# Kernel specific source files
KERNEL_PORT_SRCS := \
    ../port/print.c \
    ../port/alloc.c \
    ../port/xalloc.c \
    ../port/dev.c

KERNEL_PLAT_SRCS := \
    main.c \
    stubs.c \
    uart.c \
    sel4.c \
    sched.c \
    fgrp.c \
    exec.c \
    syscall.c \
    devcons.c \
    devenv.c \
    devpipe.c \
    devdup.c \
    devsrv.c \
    devdraw.c \
    devmouse.c \
    devroot.c \
    devmntsel4.c \
    $(BUILD_DIR)/errstr.c

KERNEL_OBJS := $(patsubst %.c,$(BUILD_DIR)/kernel/%.o,$(notdir $(KERNEL_PORT_SRCS) $(KERNEL_PLAT_SRCS)))

# Display PD sources
DISPLAY_SRCS := display/main.c
DISPLAY_ELF  := $(BUILD_DIR)/display_pd.elf

# Input PD sources
INPUT_SRCS := input/main.c
INPUT_ELF  := $(BUILD_DIR)/input_pd.elf

# ── Build Rules ──────────────────────────────────────────────────────────────

.PHONY: all clean dirs libraries

all: dirs libraries $(USER_BINS) $(TARGET_IMAGE)

dirs:
	@mkdir -p $(BUILD_DIR)/libc
	@mkdir -p $(BUILD_DIR)/libmp
	@mkdir -p $(BUILD_DIR)/libsec
	@mkdir -p $(BUILD_DIR)/libthread
	@mkdir -p $(BUILD_DIR)/libbio
	@mkdir -p $(BUILD_DIR)/kernel
	@mkdir -p $(BUILD_DIR)/display
	@mkdir -p $(BUILD_DIR)/input
	@mkdir -p $(BUILD_DIR)/cmd/rc

# Compile C files of C library
$(BUILD_DIR)/pool.c: ../../libc/port/pool.c
	@mkdir -p $(BUILD_DIR)
	sed -E -e 's/0xCAB00D1E\+1/0xCAB00D1E + 1/g' \
	       -e 's/0xC0A1E5CE\+1/0xC0A1E5CE + 1/g' \
	       -e 's/0xEC5E1A0C\+1/0xEC5E1A0C + 1/g' $< > $@

$(BUILD_DIR)/libc/pool.o: $(BUILD_DIR)/pool.c
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/date.c: ../../libc/port/date.c
	@mkdir -p $(BUILD_DIR)
	sed 's/τconv/tauconv/g' $< > $@

$(BUILD_DIR)/libc/date.o: $(BUILD_DIR)/date.c
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/libc/%.o: ../../libc/sel4/%.c
	$(CC) $(CFLAGS) -c -o $@ $<
$(BUILD_DIR)/libc/%.o: ../../libc/port/%.c
	$(CC) $(CFLAGS) -c -o $@ $<
$(BUILD_DIR)/libc/%.o: ../../libc/fmt/%.c
	$(CC) $(CFLAGS) -c -o $@ $<
$(BUILD_DIR)/libc/%.o: ../../libc/9sys/%.c
	$(CC) $(CFLAGS) -c -o $@ $<

# Compile C files of Multi-Precision library
$(BUILD_DIR)/libmp/%.o: ../../libmp/port/%.c
	$(CC) $(CFLAGS) -c -o $@ $<

# Compile C files of Security library
$(BUILD_DIR)/libsec/%.o: ../../libsec/port/%.c
	$(CC) $(CFLAGS) -c -o $@ $<

# Compile C files of Thread library
$(BUILD_DIR)/libthread/%.o: ../../libthread/%.c
	$(CC) $(CFLAGS) -c -o $@ $<
$(BUILD_DIR)/libthread/%.o: ../../libthread/sel4.c
	$(CC) $(CFLAGS) -c -o $@ $<

# Compile C files of Bio buffering library
$(BUILD_DIR)/libbio/%.o: ../../libbio/%.c
	$(CC) $(CFLAGS) -c -o $@ $<

# Rules to generate sanitized port headers
$(BUILD_DIR)/portfns.h: ../port/portfns.h
	@mkdir -p $(BUILD_DIR)
	sed 's/µs/us/g' $< > $@

$(BUILD_DIR)/portdat.h: ../port/portdat.h
	@mkdir -p $(BUILD_DIR)
	sed -E -e 's/^([[:space:]]+)Lock;/\1Lock lock_member;/g' \
	       -e 's/^([[:space:]]+)QLock;/\1QLock qlock_member;/g' \
	       -e 's/^([[:space:]]+)Rendez;/\1Rendez rendez_member;/g' \
	       -e 's/^([[:space:]]+)Ref;/\1Ref ref_member;/g' \
	       -e 's/^([[:space:]]+)RWLock;/\1RWLock rwlock_member;/g' $< > $@

# Rules to generate errstr.c from error.h
$(BUILD_DIR)/errstr.c: ../port/error.h
	@mkdir -p $(BUILD_DIR)
	sed -E -e 's/^extern //' -e 's/\[\];[[:space:]]*\/\*[[:space:]]*(.*)[[:space:]]*\*\//[] = "\1";/' $< > $@

# Compile Kernel C files
$(BUILD_DIR)/kernel/%.o: %.c $(BUILD_DIR)/portdat.h $(BUILD_DIR)/portfns.h
	$(CC) $(CFLAGS) -DCOMPILING_KERNEL -c -o $@ $<
$(BUILD_DIR)/kernel/%.o: $(BUILD_DIR)/%.c $(BUILD_DIR)/portdat.h $(BUILD_DIR)/portfns.h
	$(CC) $(CFLAGS) -DCOMPILING_KERNEL -c -o $@ $<
$(BUILD_DIR)/kernel/%.o: ../port/%.c $(BUILD_DIR)/portdat.h $(BUILD_DIR)/portfns.h
	$(CC) $(CFLAGS) -DCOMPILING_KERNEL -c -o $@ $<

# Archive Libraries
$(LIBC_A): $(LIBC_OBJS)
	$(AR) rcs $@ $^
$(LIBMP_A): $(LIBMP_OBJS)
	$(AR) rcs $@ $^
$(LIBSEC_A): $(LIBSEC_OBJS)
	$(AR) rcs $@ $^
$(LIBTHREAD_A): $(LIBTHREAD_OBJS)
	$(AR) rcs $@ $^
$(LIBBIO_A): $(LIBBIO_OBJS)
	$(AR) rcs $@ $^

libraries: $(LIBC_A) $(LIBMP_A) $(LIBSEC_A) $(LIBTHREAD_A) $(LIBBIO_A)

# Link Kernel static ELF
# --allow-multiple-definition: portdat.h/portfns.h define globals (not extern)
# in every TU (kencc behavior); linker must accept them.
$(TARGET_ELF): $(KERNEL_OBJS) $(LIBC_A) $(LIBMP_A) $(LIBSEC_A)
	$(CC) -nostdlib -ffreestanding $(CFLAGS_ARCH) \
		$(KERNEL_OBJS) \
		$(LIBC_A) $(LIBMP_A) $(LIBSEC_A) \
		-L$(BOARD_DIR)/lib -lmicrokit -Tmicrokit.ld \
		-Wl,--allow-multiple-definition \
		-o $@

# Rule to generate embedded_bins.h from compiled user binaries
$(BUILD_DIR)/embedded_bins.h: $(USER_BINS) embed_bins.py
	python3 embed_bins.py $(BUILD_DIR)

# Compile 9P server object file (without -DCOMPILING_KERNEL)
$(BUILD_DIR)/9pserver.o: 9pserver.c $(BUILD_DIR)/embedded_bins.h
	$(CC) $(CFLAGS) -DEMBED_BINARIES -c -o $@ $<

# Link 9P (rootfs) server static ELF
$(SERVER_ELF): $(BUILD_DIR)/9pserver.o $(LIBC_A) $(LIBMP_A) $(LIBSEC_A)
	$(CC) -nostdlib -ffreestanding $(CFLAGS_ARCH) \
		$< \
		$(LIBC_A) $(LIBMP_A) $(LIBSEC_A) \
		-L$(BOARD_DIR)/lib -lmicrokit -Tmicrokit.ld \
		-o $@

# Compile display PD
$(BUILD_DIR)/display/%.o: display/%.c
	$(CC) $(CFLAGS) -c -o $@ $<

# Link display PD
$(DISPLAY_ELF): $(BUILD_DIR)/display/main.o $(LIBC_A)
	$(CC) -nostdlib -ffreestanding $(CFLAGS_ARCH) \
		$< \
		$(LIBC_A) \
		-L$(BOARD_DIR)/lib -lmicrokit -Tmicrokit.ld \
		-o $@

# Compile input PD
$(BUILD_DIR)/input/%.o: input/%.c
	$(CC) $(CFLAGS) -c -o $@ $<

# Link input PD
$(INPUT_ELF): $(BUILD_DIR)/input/main.o $(LIBC_A)
	$(CC) -nostdlib -ffreestanding $(CFLAGS_ARCH) \
		$< \
		$(LIBC_A) \
		-L$(BOARD_DIR)/lib -lmicrokit -Tmicrokit.ld \
		-o $@

# Package image with Microkit tool (all 4 PDs)
$(TARGET_IMAGE): $(TARGET_ELF) $(SERVER_ELF) $(DISPLAY_ELF) $(INPUT_ELF) plan9_aarch64.system
	$(MICROKIT_SDK)/bin/microkit plan9_aarch64.system \
		--search-path $(BUILD_DIR) \
		--board $(MICROKIT_BOARD) \
		--config $(MICROKIT_CONFIG) \
		-o $@ \
		-r $(REPORT_FILE)
	@echo "========================================="
	@echo "Plan 9 Microkit build completed!"
	@echo "Target image: $(TARGET_IMAGE)"
	@echo "========================================="

# ── User Binaries Build Rules ───────────────────────────────────────────────

$(ECHO_ELF): ../../cmd/echo.c $(LIBC_A) $(LIBMP_A) $(LIBSEC_A)
	$(CC) $(CFLAGS) -o $@ $< $(LIBC_A) $(LIBMP_A) $(LIBSEC_A)

$(CAT_ELF): ../../cmd/cat.c $(LIBC_A) $(LIBMP_A) $(LIBSEC_A)
	$(CC) $(CFLAGS) -o $@ $< $(LIBC_A) $(LIBMP_A) $(LIBSEC_A)

$(LS_ELF): ../../cmd/ls.c $(LIBC_A) $(LIBMP_A) $(LIBSEC_A) $(LIBBIO_A)
	$(CC) $(CFLAGS) -o $@ $< $(LIBBIO_A) $(LIBC_A) $(LIBMP_A) $(LIBSEC_A)

$(PWD_ELF): ../../cmd/pwd.c $(LIBC_A) $(LIBMP_A) $(LIBSEC_A)
	$(CC) $(CFLAGS) -o $@ $< $(LIBC_A) $(LIBMP_A) $(LIBSEC_A)

$(MKDIR_ELF): ../../cmd/mkdir.c $(LIBC_A) $(LIBMP_A) $(LIBSEC_A)
	$(CC) $(CFLAGS) -o $@ $< $(LIBC_A) $(LIBMP_A) $(LIBSEC_A)

$(RM_ELF): ../../cmd/rm.c $(LIBC_A) $(LIBMP_A) $(LIBSEC_A)
	$(CC) $(CFLAGS) -o $@ $< $(LIBC_A) $(LIBMP_A) $(LIBSEC_A)

# rc shell compilation
RC_SRCS := \
	../../cmd/rc/code.c \
	../../cmd/rc/exec.c \
	../../cmd/rc/getflags.c \
	../../cmd/rc/glob.c \
	../../cmd/rc/here.c \
	../../cmd/rc/io.c \
	../../cmd/rc/lex.c \
	../../cmd/rc/pcmd.c \
	../../cmd/rc/pfnc.c \
	../../cmd/rc/simple.c \
	../../cmd/rc/subr.c \
	../../cmd/rc/trap.c \
	../../cmd/rc/tree.c \
	../../cmd/rc/var.c \
	../../cmd/rc/havefork.c \
	../../cmd/rc/plan9.c \
	../../cmd/rc/syn.c

RC_OBJS := $(patsubst %.c,$(BUILD_DIR)/cmd/rc/%.o,$(notdir $(RC_SRCS)))

$(BUILD_DIR)/cmd/rc/%.o: ../../cmd/rc/%.c
	$(CC) $(CFLAGS) -DPlan9 -c -o $@ $<

$(RC_ELF): $(RC_OBJS) $(LIBC_A) $(LIBMP_A) $(LIBSEC_A) $(LIBBIO_A)
	$(CC) -nostdlib -ffreestanding $(CFLAGS_ARCH) -o $@ $(RC_OBJS) $(LIBBIO_A) $(LIBC_A) $(LIBMP_A) $(LIBSEC_A)

clean:
	rm -rf $(BUILD_DIR)/libc
	rm -rf $(BUILD_DIR)/libmp
	rm -rf $(BUILD_DIR)/libsec
	rm -rf $(BUILD_DIR)/libthread
	rm -rf $(BUILD_DIR)/libbio
	rm -rf $(BUILD_DIR)/kernel
	rm -rf $(BUILD_DIR)/display
	rm -rf $(BUILD_DIR)/input
	rm -rf $(BUILD_DIR)/cmd
	rm -f $(LIBC_A) $(LIBMP_A) $(LIBSEC_A) $(LIBTHREAD_A) $(LIBBIO_A)
	rm -f $(TARGET_ELF) $(SERVER_ELF) $(DISPLAY_ELF) $(INPUT_ELF) $(USER_BINS)
	rm -f $(TARGET_IMAGE) $(REPORT_FILE)
	rm -f $(BUILD_DIR)/9pserver.o
	rm -f $(BUILD_DIR)/portdat.h $(BUILD_DIR)/portfns.h $(BUILD_DIR)/errstr.c $(BUILD_DIR)/pool.c $(BUILD_DIR)/date.c $(BUILD_DIR)/embedded_bins.h
