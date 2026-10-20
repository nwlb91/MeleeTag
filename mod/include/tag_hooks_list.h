#ifndef MELEETAG_HOOKS_LIST_H
#define MELEETAG_HOOKS_LIST_H

/*
 * Every engine function the payload detours, as
 * X(engine function, handler, trampoline).  The handler replaces the
 * function and calls the original through the trampoline (tag_hook.h).
 *
 *   fn_8016E730              versus match load (local and Slippi online)
 *   lbDvd_80017960           preload registration of a scene's characters
 *   lbAudioAx_8002785C       match sound bank load
 *   mnCharSel_Scene_OnEnter  character select screen setup
 *   mnCharSel_Scene_OnFrame  character select screen frame
 *   PADRead                  controller read (netplay announcement)
 *
 * tools/slippi_conflicts.py checks at build time that Slippi's codes do
 * not patch any of these first instructions, and the patcher refuses a
 * first instruction that is PC-relative.
 */
#define TAG_HOOKS(X)                                                          \
    X(fn_8016E730, tag_hook_fn_8016E730, tag_tramp_fn_8016E730)               \
    X(lbDvd_80017960, tag_hook_lbDvd_80017960, tag_tramp_lbDvd_80017960)      \
    X(lbAudioAx_8002785C, tag_hook_lbAudioAx_8002785C,                        \
      tag_tramp_lbAudioAx_8002785C)                                           \
    X(mnCharSel_Scene_OnEnter, tag_hook_mnCharSel_Scene_OnEnter,              \
      tag_tramp_mnCharSel_Scene_OnEnter)                                      \
    X(mnCharSel_Scene_OnFrame, tag_hook_mnCharSel_Scene_OnFrame,              \
      tag_tramp_mnCharSel_Scene_OnFrame)                                      \
    X(PADRead, tag_hook_PADRead, tag_tramp_PADRead)

#define TAG_HOOK_DECLARE(site, handler, tramp)                                \
    extern void site(void);                                                   \
    extern void handler(void);                                                \
    extern unsigned int tramp[2];

TAG_HOOKS(TAG_HOOK_DECLARE)

#endif
