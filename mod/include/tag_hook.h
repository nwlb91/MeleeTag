#ifndef MELEETAG_HOOK_H
#define MELEETAG_HOOK_H

/*
 * Function-entry detours.
 *
 * At boot the first instruction of each hooked engine function is replaced by
 * a branch to its handler.  The handler has the hooked function's exact C
 * signature, so it is called with the original arguments and its return value
 * is the function's return value.  To run the engine's version, a handler
 * calls through the hook's trampoline: the displaced first instruction
 * followed by a branch to the second instruction.  The patcher only accepts
 * hook sites whose first instruction is position independent.
 *
 * TAG_TRAMPOLINE(name) declares the trampoline storage; TAG_CALL_ORIG casts
 * it to the hooked function's type.
 */

#define TAG_TRAMPOLINE(name)                                                  \
    unsigned int name[2] __attribute__((aligned(8), section(".data.tramp")))

#define TAG_CALL_ORIG(tramp, type) ((type) (void*) (tramp))

#endif
