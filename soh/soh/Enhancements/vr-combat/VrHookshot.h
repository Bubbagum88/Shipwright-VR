#pragma once
// SOH [VR] Hookshot grapple cheat ("Spider-Man module"), see VrHookshot.c.
#ifdef __cplusplus
extern "C" {
#endif

struct Player;
struct PlayState;

void VrHookshot_OnCatch(struct Player* player);
void VrHookshot_RopeStep(struct Player* player, struct PlayState* play, signed char stickY);
void VrHookshot_AddScaled(Vec3f* v, Vec3f* n, f32 s);

#ifdef __cplusplus
}
#endif
