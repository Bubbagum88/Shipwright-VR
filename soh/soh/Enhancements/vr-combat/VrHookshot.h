#pragma once
// SOH [VR] Hookshot grapple cheat ("Spider-Man module"), see VrHookshot.c.
#ifdef __cplusplus
extern "C" {
#endif

struct Player;
struct PlayState;
struct ArmsHook;

void VrHookshot_OnCatch(struct Player* player);
// Dual hookshots
int32_t VrHookshot_DualActive(void);
int32_t VrHookshot_HookHand(struct ArmsHook* hook);
int32_t VrHookshot_TriggerHeld(struct ArmsHook* hook);
int32_t VrHookshot_TriggerConsumed(int32_t vrHand, uint16_t vrBtnMask);
struct ArmsHook* VrHookshot_FindHook(struct PlayState* play, int32_t second);
void VrHookshot_Tick(struct Player* player, struct PlayState* play);
s32 VrHookshot_HangHookHome(struct Player* player);
void VrHookshot_RopeStep(struct Player* player, struct PlayState* play, signed char stickY);
void VrHookshot_AddScaled(Vec3f* v, Vec3f* n, f32 s);

#ifdef __cplusplus
}
#endif
