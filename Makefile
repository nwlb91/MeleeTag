# MeleeTag build.
#
#   make            payload + patcher (host) + Windows patcher if mingw exists
#   make payload    build/payload.bin (the code that goes into the DOL)
#   make patcher    build/meleetag-patch (host)
#   make windows    build/meleetag-patch.exe
#   make test       host unit tests, qemu-ppc payload tests, patcher tests
#   make dist       dist/ folder with the patcher, .bat and README
#
# Needs: powerpc-linux-gnu-gcc/ld, python3, a host cc; optional
# x86_64-w64-mingw32-gcc and qemu-ppc for the Windows build and tests.

PPC_PREFIX ?= powerpc-linux-gnu-
PPC_CC := $(PPC_PREFIX)gcc
PPC_LD := $(PPC_PREFIX)ld
HOST_CC ?= cc
MINGW_CC ?= x86_64-w64-mingw32-gcc
PYTHON ?= python3

DECOMP := third_party/melee
BUILD := build

PPC_CFLAGS := -m32 -mcpu=750 -mhard-float -mbig-endian -std=gnu99 \
	-nostdinc -ffreestanding -fno-builtin -fno-pic -mno-relocatable \
	-msdata=none -G 0 -ffixed-r2 -ffixed-r13 -O2 -fno-common \
	-fno-asynchronous-unwind-tables -ffunction-sections -fdata-sections \
	-Wall -Wextra -Wno-unused-parameter -Werror \
	-I mod/include \
	-isystem $(DECOMP)/src/MSL -isystem $(DECOMP)/src \
	-isystem $(DECOMP)/libs/dolphin/include \
	-isystem $(DECOMP)/libs/doldecomp/include

PAYLOAD_SRCS := \
	mod/src/payload_header.c \
	mod/src/boot.c \
	mod/src/tag_init.c \
	mod/src/tag_rules.c \
	mod/src/tag_persist.c \
	mod/src/tag_switch.c \
	mod/src/tag_preload.c \
	mod/src/tag_match.c \
	mod/src/tag_hud.c \
	mod/src/tag_css.c \
	mod/src/tag_net.c
PAYLOAD_ASM := mod/src/boot_entry.S

PAYLOAD_OBJS := $(PAYLOAD_SRCS:mod/src/%.c=$(BUILD)/ppc/%.o) \
	$(PAYLOAD_ASM:mod/src/%.S=$(BUILD)/ppc/%.o)

HOST_CFLAGS := -std=c99 -O2 -Wall -Wextra -Werror

.PHONY: all payload patcher windows test dist clean check-slippi

all: payload patcher $(if $(shell command -v $(MINGW_CC)),windows)

$(BUILD)/gale01_syms.ld: $(DECOMP)/config/GALE01/symbols.txt tools/gen_ldsyms.py
	@mkdir -p $(BUILD)
	$(PYTHON) tools/gen_ldsyms.py $< $@

$(BUILD)/ppc/%.o: mod/src/%.c $(wildcard mod/include/*.h) | $(BUILD)/ppc
	$(PPC_CC) $(PPC_CFLAGS) -c $< -o $@

$(BUILD)/ppc/%.o: mod/src/%.S | $(BUILD)/ppc
	$(PPC_CC) $(PPC_CFLAGS) -c $< -o $@

$(BUILD)/ppc:
	@mkdir -p $@

$(BUILD)/payload.elf: $(PAYLOAD_OBJS) mod/payload.ld $(BUILD)/gale01_syms.ld
	$(PPC_LD) -q --gc-sections -T mod/payload.ld -o $@ $(PAYLOAD_OBJS)

$(BUILD)/payload.bin $(BUILD)/payload.h $(BUILD)/payload.sites: $(BUILD)/payload.elf tools/mkpayload.py
	$(PYTHON) tools/mkpayload.py $< $(BUILD)/payload

payload: $(BUILD)/payload.bin check-slippi

# Every engine word the mod patches must be one Slippi's codes leave alone.
check-slippi: $(BUILD)/payload.sites
	$(PYTHON) tools/slippi_conflicts.py $(BUILD)/payload.sites \
		third_party/slippi-ssbm-asm/Output/Netplay/GALE01r2.ini \
		third_party/slippi-ssbm-asm/Output/Playback/GALE01r2.ini

PATCHER_SRCS := patcher/patcher.c patcher/iso.c patcher/dol.c patcher/sha1.c
PATCHER_HDRS := $(wildcard patcher/*.h) $(BUILD)/payload.h

$(BUILD)/meleetag-patch: $(PATCHER_SRCS) $(PATCHER_HDRS)
	$(HOST_CC) $(HOST_CFLAGS) -I $(BUILD) -o $@ $(PATCHER_SRCS)

$(BUILD)/meleetag-patch.exe: $(PATCHER_SRCS) $(PATCHER_HDRS)
	$(MINGW_CC) $(HOST_CFLAGS) -static -I $(BUILD) -o $@ $(PATCHER_SRCS) -lshell32

patcher: $(BUILD)/meleetag-patch
windows: $(BUILD)/meleetag-patch.exe

test: payload patcher
	$(MAKE) -C tests

dist: all
	@mkdir -p dist
	cp $(BUILD)/meleetag-patch dist/
	-cp $(BUILD)/meleetag-patch.exe dist/
	cp patcher/MeleeTag.bat patcher/meleetag.sh dist/
	cp README.md dist/README.md

clean:
	rm -rf $(BUILD)/ppc $(BUILD)/payload.* $(BUILD)/meleetag-patch* dist
	$(MAKE) -C tests clean
