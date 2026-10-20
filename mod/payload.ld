/*
 * MeleeTag payload link script.  Everything goes into one output section at
 * TAG_LINK_BASE (see mod/include/tag_payload.h); tools/mkpayload.py turns the
 * linked ELF into the relocatable image the patcher embeds.
 */
OUTPUT_FORMAT("elf32-powerpc")
OUTPUT_ARCH(powerpc:common)
ENTRY(tag_boot_entry)

SECTIONS
{
    . = 0x80A00000;
    .payload : {
        KEEP(*(.payload_header))
        KEEP(*(.text.boot_entry))
        *(.text.boot .text.boot.*)
        *(.text .text.*)
        . = ALIGN(8);
        *(.rodata .rodata.* .rodata1)
        . = ALIGN(8);
        *(.data .data.* .data1)
        *(.data.tramp)
        *(.sdata .sdata.* .sdata2 .sdata2.*)
        . = ALIGN(8);
        tag_bss_start = .;
        *(.sbss .sbss.* .sbss2 .sbss2.*)
        *(.bss .bss.* COMMON)
        tag_bss_end = .;
        . = ALIGN(32);
    }
    tag_image_end = .;

    /DISCARD/ : {
        *(.comment) *(.note .note.*) *(.eh_frame .eh_frame_hdr)
        *(.gnu.attributes) *(.got .got2 .plt .dynamic .dynsym .dynstr)
    }
}

INCLUDE build/gale01_syms.ld
