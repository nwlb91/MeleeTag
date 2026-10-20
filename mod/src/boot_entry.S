/*
 * Boot entry, reached by the branch the patcher writes over HSD_OSInit's
 * first instruction.  HSD_OSInit takes no arguments, so only LR (the return
 * address into HSD_InitComponent) needs preserving.  tag_boot() restores the
 * original first instruction, so branching back to HSD_OSInit runs it intact.
 *
 * This section is linked at TAG_LINK_BASE and is never executed again after
 * boot, so it may live in the scratch copy that the heaps later overwrite.
 */
    .section .text.boot_entry, "ax"
    .globl tag_boot_entry
tag_boot_entry:
    stwu    1, -32(1)
    mflr    0
    stw     0, 36(1)
    bl      tag_boot
    lwz     0, 36(1)
    mtlr    0
    addi    1, 1, 32
    b       HSD_OSInit

    .section .note.GNU-stack,"",@progbits
