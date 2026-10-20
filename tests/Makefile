# MeleeTag tests.  Run from the top level with `make test`.
#
#   test_rules    host unit tests of the rule logic (same source as the payload)
#   boot          the real payload's boot relocator, executed under qemu-ppc
#   patcher       the ISO patcher on synthetic disc images
#
# qemu-ppc and a static powerpc libc are needed for `boot`; it is skipped
# with a notice when they are missing.

ROOT := ..
BUILD := $(ROOT)/build
OUT := out
HOST_CC ?= cc
PPC_CC ?= powerpc-linux-gnu-gcc
PPC_NM ?= powerpc-linux-gnu-nm
QEMU ?= qemu-ppc
PYTHON ?= python3

.PHONY: all rules boot patcher clean

all: rules boot patcher

$(OUT):
	@mkdir -p $@

$(OUT)/test_rules: test_rules.c $(ROOT)/mod/src/tag_rules.c $(ROOT)/mod/include/tag_rules.h | $(OUT)
	$(HOST_CC) -std=c99 -O2 -Wall -Wextra -Werror -I $(ROOT)/mod/include \
		-o $@ test_rules.c $(ROOT)/mod/src/tag_rules.c

rules: $(OUT)/test_rules
	$(OUT)/test_rules

$(OUT)/boot_syms.h: qemu/gen_syms.py $(BUILD)/payload.elf $(BUILD)/payload.sites | $(OUT)
	$(PYTHON) qemu/gen_syms.py $(BUILD)/gale01_syms.ld $(BUILD)/payload.elf \
		$(BUILD)/payload.sites $@ $(PPC_NM)

$(OUT)/boot_test: qemu/boot_test.c $(OUT)/boot_syms.h $(ROOT)/mod/include/tag_rules.h
	$(PPC_CC) -static -O2 -Wall -Wextra -I $(OUT) -I $(ROOT)/mod/include \
		-o $@ qemu/boot_test.c

boot:
	@if command -v $(QEMU) >/dev/null 2>&1; then \
		$(MAKE) --no-print-directory $(OUT)/boot_test && \
		$(QEMU) $(OUT)/boot_test $(BUILD)/payload.bin normal && \
		$(QEMU) $(OUT)/boot_test $(BUILD)/payload.bin small-arena; \
	else \
		echo "boot: $(QEMU) not found, skipped"; \
	fi

patcher:
	$(PYTHON) test_patcher.py $(BUILD)/meleetag-patch $(BUILD)/payload.bin \
		$(BUILD)/payload.sites $(OUT)/iso

clean:
	rm -rf $(OUT)
