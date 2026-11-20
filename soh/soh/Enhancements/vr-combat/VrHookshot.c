// SOH [VR] Hookshot grapple cheat ("Spider-Man module"): the rope physics, moved out of z_player.c
// unchanged. z_player.c keeps only the calls at the points where the game hands Link to the rope
// (catch, each tick of the hookshot flying action). The rope state lives here.
#include <libultraship/libultra.h>
#include "global.h"
#include "soh/Enhancements/vr-combat/VrCombat.h"
#include "soh/Enhancements/vr-combat/VrHookshot.h"

static f32 sVrHookRopeLen = 0.0f;   // current rope length
static s32 sVrHookTugging = false;  // a tug is in progress (one burst per tug)

// The hook just caught: the rope starts at the distance from the gun barrel to the hook point.
void VrHookshot_OnCatch(Player* this) {
    sVrHookRopeLen = Math_Vec3f_DistXYZ(&this->unk_3C8, &this->actor.parent->world.pos);
    sVrHookTugging = false;
}

// v += n * s. Used for every push along the rope, and for the wall push in z_player.c.
void VrHookshot_AddScaled(Vec3f* v, Vec3f* n, f32 s) {
    v->x += n->x * s;
    v->y += n->y * s;
    v->z += n->z * s;
}

// One tick of the rope pulling Link toward the hook point. Builds this tick's velocity; the hookshot
// flying action (Player_Action_80850AEC) moves him by it as it always has. stickY is the move
// stick's forward axis (sControlInput->rel.stick_y in z_player.c).
void VrHookshot_RopeStep(Player* this, PlayState* play, s8 stickY) {
    Vec3f* vel = &this->actor.velocity;
    Vec3f n; // unit vector from Link toward the hook
    Vec3f next;
    f32 dist;
    f32 speed;
    f32 nextDist;

    // The game's own player gravity, scaled by the Swing Gravity setting.
    vel->y += REG(68) / 100.0f * CVarGetFloat(CVAR_CHEAT("VrHookshotGravity"), 1.0f);

    dist = Math_Vec3f_DistXYZAndStoreDiff(&this->unk_3C8, &this->actor.parent->world.pos, &n);
    // Never hand over to the vanilla pull while the trigger is held: its arrival stops Link dead (forward
    // speed 1, vertical 0) and drops him into the fall. He stays on the rope until he lets go.
    if (dist < 1.0f) {
        return;
    }
    Math_Vec3f_Scale(&n, 1.0f / dist);

    if ((play->gameplayFrames % 5) == 0) {
        LUSLOG_INFO("[VrHook] rope: dist=%.0f len=%.0f vel=(%.1f, %.1f, %.1f) floor=%.0f", dist, sVrHookRopeLen,
                    vel->x, vel->y, vel->z, this->actor.world.pos.y - this->actor.floorHeight);
    }

    if (CVarGetInteger(CVAR_CHEAT("VrHookshotMode"), 0) == 2) {
        // Both: the Spider-Man rope (and its tug, below), reeled with the move stick (unused on the rope):
        // forward pulls toward the hook like Grapple and the rope follows; back lets rope out.
        f32 stick = CLAMP(stickY / 60.0f, -1.0f, 1.0f);

        if (stick > 0.2f) {
            VrHookshot_AddScaled(vel, &n, stick * CVarGetFloat(CVAR_CHEAT("VrHookshotPull"), 3.0f));
        } else if (stick < -0.2f) {
            sVrHookRopeLen -= stick * CVarGetFloat(CVAR_CHEAT("VrHookshotReelOut"), 10.0f);
        }
    }

    if (CVarGetInteger(CVAR_CHEAT("VrHookshotMode"), 0) == 0) {
        // Grapple: accelerate toward the hook point; momentum carries everything else. Once within Min Rope
        // of it, stop pulling and hang there on a rope of that length (the shared rope below) until let go.
        f32 minRope = CVarGetFloat(CVAR_CHEAT("VrHookshotMinRope"), 50.0f);

        if (sVrHookRopeLen > minRope) {
            VrHookshot_AddScaled(vel, &n, CVarGetFloat(CVAR_CHEAT("VrHookshotPull"), 3.0f));
            if (dist > minRope) {
                return;
            }
            sVrHookRopeLen = minRope;
        }
    } else {
        // Spider-Man: a fixed-length rope. A tug (the hook hand pulled back toward you) gives one burst
        // along the rope and shortens it to where it ends up.
        Vec3f hand; // m/s, game axes
        f32 pull;
        f32 tugAt = CVarGetFloat(CVAR_CHEAT("VrHookshotTugThreshold"), 1.5f); // m/s

        VrCombat_AimHandVelocity(&hand.x);
        pull = -DOTXYZ(hand, n); // hand moving away from the hook
        if (pull > tugAt) {
            if (!sVrHookTugging) {
                VrHookshot_AddScaled(vel, &n,
                                        pull * VrCombat_WorldScale() / 20.0f *
                                            CVarGetFloat(CVAR_CHEAT("VrHookshotTug"), 1.0f));
                sVrHookTugging = true;
            }
        } else if (pull < tugAt / 3.0f) {
            sVrHookTugging = false;
        }
    }

    // The rope (Spider-Man always; Grapple once it has reeled in to Min Rope). Moved out of the
    // Spider-Man branch unchanged.
    speed = Math3D_Vec3fMagnitude(vel);
    Math_Vec3f_Sum(&this->unk_3C8, vel, &next);
    nextDist = Math_Vec3f_DistXYZAndStoreDiff(&next, &this->actor.parent->world.pos, &n);
    if (nextDist < sVrHookRopeLen) {
        // A tug or a swing upward took up the slack.
        sVrHookRopeLen = MAX(nextDist, CVarGetFloat(CVAR_CHEAT("VrHookshotMinRope"), 50.0f));
    } else if (nextDist > 0.0f) {
        f32 outward;

        Math_Vec3f_Scale(&n, 1.0f / nextDist);
        // Drop the velocity going away from the hook, then give the swing back the speed it had
        // (capped at Swing Speed Cap times the remaining swing speed, so falling straight under the
        // hook stops instead of jerking sideways).
        outward = -DOTXYZ((*vel), n);
        if (outward > 0.0f) {
            f32 tangent;

            VrHookshot_AddScaled(vel, &n, outward);
            tangent = Math3D_Vec3fMagnitude(vel);
            if (tangent > 0.001f) {
                Math_Vec3f_Scale(vel, MIN(speed, tangent * CVarGetFloat(CVAR_CHEAT("VrHookshotSwingCap"), 2.0f)) / tangent);
            }
        }
        // Pull back to rope length, so gravity can't stretch the rope a little every tick.
        VrHookshot_AddScaled(vel, &n, nextDist - sVrHookRopeLen);
    }
}
