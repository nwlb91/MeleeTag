/*
 * Payload header and hook table.  The header must be the first bytes of the
 * image (see payload.ld); tools/mkpayload.py fills the size, relocation and
 * build-id fields after linking and the patcher fills boot_orig and every
 * hook's orig_insn from the user's DOL.
 */
#include "tag_hooks_list.h"
#include "tag_payload.h"

extern void tag_boot_entry(void);
extern void tag_init(void);
extern TagConfig tag_config;

#define TAG_HOOK_ENTRY(site, handler, tramp)                                  \
    { (unsigned int) &site, (unsigned int) &handler, (unsigned int) tramp, 0 },

static const TagHook tag_hook_table[] = { TAG_HOOKS(TAG_HOOK_ENTRY) };

__attribute__((section(".payload_header"), used))
TagPayloadHeader tag_payload_header = {
    TAG_PAYLOAD_MAGIC,
    TAG_PAYLOAD_VERSION,
    0, /* image_size: mkpayload.py */
    0, /* reloc_off: mkpayload.py */
    0, /* reloc_count: mkpayload.py */
    (unsigned int) tag_hook_table,
    sizeof(tag_hook_table) / sizeof(tag_hook_table[0]),
    (unsigned int) &tag_init,
    (unsigned int) &tag_boot_entry,
    TAG_BOOT_SITE,
    0, /* boot_orig: patcher */
    (unsigned int) &tag_config,
    0, /* build_id: mkpayload.py */
    { 0, 0, 0 },
};

TagConfig tag_config = {
    TAG_CONFIG_MAGIC,
    TAG_DEFAULT_COOLDOWN,
    TAG_CFG_HUD | TAG_CFG_NETSYNC,
    TAG_DEFAULT_MAX_FIGHTERS,
    { 0, 0, 0, 0 },
};
