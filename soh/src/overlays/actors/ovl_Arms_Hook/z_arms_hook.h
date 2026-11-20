#ifndef Z_ARMS_HOOK_H
#define Z_ARMS_HOOK_H

#include <libultraship/libultra.h>
#include "global.h"

struct ArmsHook;

typedef void (*ArmsHookActionFunc)(struct ArmsHook*, PlayState*);

typedef struct ArmsHook {
    /* 0x0000 */ Actor actor;
    /* 0x014C */ ColliderQuad collider;
    /* 0x01CC */ WeaponInfo hookInfo;
    /* 0x01E8 */ Vec3f unk_1E8;
    /* 0x01F4 */ Vec3f unk_1F4;
    /* 0x0200 */ Actor* grabbed;
    /* 0x0204 */ Vec3f grabbedDistDiff;
    /* 0x0210 */ s16 timer;
    /* 0x0214 */ ArmsHookActionFunc actionFunc;
    // SOH [VR] Dual hookshots: the second hook (sword hand) keeps its own barrel origin and never
    // writes Link's held-item fields; the first hook is the vanilla one and leaves these zero.
    s8 vrSecond;
    Vec3f vrOrigin;
} ArmsHook;

void ArmsHook_ReleaseLine(ArmsHook* this, Player* player);
// SOH [VR] Where the line leaves the gun: Link's hookshot origin, or the second hook's own.
Vec3f* ArmsHook_Origin(ArmsHook* this, Player* player);
// SOH [VR] In the gun, ready to fire (the second hook's equivalent of Link's heldActor).
s32 ArmsHook_IsLoaded(ArmsHook* this);

#endif
