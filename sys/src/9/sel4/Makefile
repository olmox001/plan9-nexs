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
INCS := -I$(BUILD_DIR) -I../../../../sel4/include -I../../../include -I. -I../../libmp/port -I$(BOARD_DIR)/include
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

# ── Source File Definitions ──────────────────────────────────────────────────

# libc/sel4/ custom files
LIBC_SEL4_SRCS := \
    ../../libc/sel4/tas.c \
    ../../libc/sel4/atom.c \
    ../../libc/sel4/getcallerpc.c \
    ../../libc/sel4/setlabel.c \
    ../../libc/sel4/fmtstubs.c

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
    ../../libc/port/utflen.c

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
    ../../libc/fmt/fmtfd.c

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
    ../../libc/9sys/fcallfmt.c

# Compile all C library files into a single list of objects
LIBC_OBJS := $(patsubst %.c,$(BUILD_DIR)/libc/%.o,$(notdir $(LIBC_SEL4_SRCS) $(LIBC_PORT_SRCS) $(LIBC_FMT_SRCS) $(LIBC_9SYS_SRCS)))

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

all: dirs libraries $(TARGET_IMAGE)

dirs:
	@mkdir -p $(BUILD_DIR)/libc
	@mkdir -p $(BUILD_DIR)/libmp
	@mkdir -p $(BUILD_DIR)/libsec
	@mkdir -p $(BUILD_DIR)/libthread
	@mkdir -p $(BUILD_DIR)/kernel
	@mkdir -p $(BUILD_DIR)/display
	@mkdir -p $(BUILD_DIR)/input

# Compile C files of C library
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

libraries: $(LIBC_A) $(LIBMP_A) $(LIBSEC_A) $(LIBTHREAD_A)

# Link Kernel static ELF
# --allow-multiple-definition: portdat.h/portfns.h define globals (not extern)
# in every TU (kencc behavior); linker must accept them.
$(TARGET_ELF): $(KERNEL_OBJS) $(LIBC_A) $(LIBMP_A) $(LIBSEC_A) $(LIBTHREAD_A)
	$(CC) -nostdlib -ffreestanding $(CFLAGS_ARCH) \
		$(KERNEL_OBJS) \
		$(LIBC_A) $(LIBMP_A) $(LIBSEC_A) $(LIBTHREAD_A) \
		-L$(BOARD_DIR)/lib -lmicrokit -Tmicrokit.ld \
		-Wl,--allow-multiple-definition \
		-o $@

# Compile 9P server object file (without -DCOMPILING_KERNEL)
$(BUILD_DIR)/9pserver.o: 9pserver.c
	$(CC) $(CFLAGS) -c -o $@ $<

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

clean:
	rm -rf $(BUILD_DIR)/libc
	rm -rf $(BUILD_DIR)/libmp
	rm -rf $(BUILD_DIR)/libsec
	rm -rf $(BUILD_DIR)/libthread
	rm -rf $(BUILD_DIR)/kernel
	rm -rf $(BUILD_DIR)/display
	rm -rf $(BUILD_DIR)/input
	rm -f $(LIBC_A) $(LIBMP_A) $(LIBSEC_A) $(LIBTHREAD_A)
	rm -f $(TARGET_ELF) $(SERVER_ELF) $(DISPLAY_ELF) $(INPUT_ELF)
	rm -f $(TARGET_IMAGE) $(REPORT_FILE)
	rm -f $(BUILD_DIR)/9pserver.o
	rm -f $(BUILD_DIR)/portdat.h $(BUILD_DIR)/portfns.h $(BUILD_DIR)/errstr.c
