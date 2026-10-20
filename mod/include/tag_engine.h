#ifndef MELEETAG_ENGINE_H
#define MELEETAG_ENGINE_H

/*
 * Engine declarations.  Functions and types come from the doldecomp/melee
 * headers (third_party/melee); everything the decomp keeps file-local is
 * declared here with the retail symbol name, and the link script resolves
 * all of them to NTSC 1.02 addresses (tools/gen_ldsyms.py).
 */

#include <dolphin/dvd.h>
#include <dolphin/os.h>
#include <dolphin/pad.h>

#include <sysdolphin/baselib/archive.h>
#include <sysdolphin/baselib/controller.h>
#include <sysdolphin/baselib/gobj.h>
#include <sysdolphin/baselib/gobjgxlink.h>
#include <sysdolphin/baselib/gobjplink.h>
#include <sysdolphin/baselib/gobjproc.h>
#include <sysdolphin/baselib/gobjuserdata.h>
#include <sysdolphin/baselib/memory.h>
#include <sysdolphin/baselib/random.h>
#include <sysdolphin/baselib/sislib.h>

#include <melee/ef/efasync.h>
#include <melee/ef/types.h>
#include <melee/ft/fighter.h>
#include <melee/ft/ft_0877.h>
#include <melee/ft/ft_081B.h>
#include <melee/ft/ft_0881.h>
#include <melee/ft/ft_0892.h>
#include <melee/ft/ft_0C88.h>
#include <melee/ft/ftcamera.h>
#include <melee/ft/ftchangeparam.h>
#include <melee/ft/ftcoll.h>
#include <melee/ft/ftcolanim.h>
#include <melee/ft/ftcommon.h>
#include <melee/ft/ftdata.h>
#include <melee/ft/ftlib.h>
#include <melee/ft/ftmetal.h>
#include <melee/ft/kinds/ftCommon/ftCo_DamageFall.h>
#include <melee/ft/kinds/ftCommon/ftCo_Fall.h>
#include <melee/ft/kinds/ftCommon/ftCo_HammerWait.h>
#include <melee/ft/kinds/ftCommon/ftCo_SquatWait.h>
#include <melee/ft/kinds/ftCommon/ftpickupitem.h>
#include <melee/ft/kinds/ftKirby/ftkirby.h>
#include <melee/ft/kinds/ftLink/ftlinkspecials.h>
#include <melee/ft/types.h>
#include <melee/gm/gm_1601.h>
#include <melee/if/ifall.h>
#include <melee/it/inlines.h>
#include <melee/it/it_26B1.h>
#include <melee/it/item.h>
#include <melee/it/kinds/itrabbitc.h>
#include <melee/lb/lb_00B0.h>
#include <melee/lb/lbaudio_ax.h>
#include <melee/lb/lbdvd.h>
#include <melee/lb/lbfile.h>
#include <melee/lb/lbheap.h>
#include <melee/lb/types.h>
#include <melee/mn/types.h>
#include <melee/pl/player.h>
#include <melee/pl/plbonuslib.h>

/* ---- pl/player.c ---- */

typedef struct TagFtMapping {
    s8 internal_id;
    s8 extra_internal_id;
    s8 has_transformation;
} TagFtMapping;

extern TagFtMapping tag_ft_mapping[] __asm__("ftMapping_list");
extern StaticPlayer tag_player_slots[] __asm__("player_slots");

/* ---- ft/ftdata.c ---- */

typedef struct TagStringPair {
    char* a;
    char* b;
} TagStringPair;

extern TagStringPair ftData_803C1F40[];             /* Pl<X>.dat, data name */
extern Fighter_CostumeStrings* ftData_803C2360[];   /* costume files */
extern char* ftData_803C23E4[];                     /* Pl<X>AJ.dat */

/* ---- lb/lbdvd.c, lb/lbheap.c ---- */

extern PreloadCache preloadCache;
void lbDvd_800189EC(int entry_num);

typedef struct TagLbHeap {
    s32 id;
    void* handle;
    uintptr_t start;
    u32 size;
    s32 type;
    s32 transient;
    s32 status;
} TagLbHeap;

typedef struct TagLbHeapState {
    void* arena_lo;
    void* arena_hi;
    uintptr_t aram_lo;
    uintptr_t aram_hi;
    TagLbHeap heap[LbHeapKind_Count];
} TagLbHeapState;

extern TagLbHeapState tag_lbheap __asm__("lbHeap_80431FA0");

/* ---- lb/lbaudio_ax.c ---- */

#define TAG_SSM_BANKS 0x38
extern int tag_ssm_want[TAG_SSM_BANKS] __asm__("lbl_804337C4");
extern int tag_ssm_loaded[TAG_SSM_BANKS] __asm__("lbl_80433984");
extern u32 tag_ssm_size[TAG_SSM_BANKS][2] __asm__("offsets_arr_803BC4E4");
extern s8 tag_ssm_info[TAG_SSM_BANKS][4] __asm__("s32_arr_803BB5D0");
extern int tag_ssm_capacity __asm__("lbl_804D6438");

/* ---- mn/mncharsel.c ---- */

struct TagCssCursor {
    HSD_GObj* gobj;
    u8 port;
    u8 state;
    u8 held_door;
    u8 x7;
    u16 x8;
    u16 xA;
    f32 x;
    f32 y;
};

extern struct TagCssCursor* tag_css_cursor[4] __asm__("mnCharSel_804A0BC0");
extern CSSIcon tag_css_icons[25 + 1] __asm__("icons");
extern CSSDoorsData tag_css_doors __asm__("mnCharSel_803F0DFC");
extern u8 tag_css_door_count __asm__("mnCharSel_804D6CF5");
extern u8 tag_css_local_port __asm__("mnCharSel_804D6CF0");
extern CSSData* mnCharSel_804D6CB0;

/* Scene controller word at 0x80479D30: major, pending, previous, minor. */
#define TAG_SCENE_CONTROLLER ((volatile u8*) 0x80479D30)
#define TAG_MAJOR_VS 0x02
#define TAG_MAJOR_ONLINE 0x08
#define TAG_MINOR_CSS 0x00
#define TAG_MINOR_INGAME 0x02

static inline u8 tag_scene_major(void)
{
    return TAG_SCENE_CONTROLLER[0];
}

static inline u8 tag_scene_minor(void)
{
    return TAG_SCENE_CONTROLLER[3];
}

#endif
