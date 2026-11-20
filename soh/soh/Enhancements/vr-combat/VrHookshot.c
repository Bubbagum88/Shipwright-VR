// SOH [VR] Hookshot grapple cheat ("Spider-Man module"): the rope physics, moved out of z_player.c
// unchanged. z_player.c keeps only the calls at the points where the game hands Link to the rope
// (catch, each tick of the hookshot flying action). The rope state lives here.
#include <libultraship/libultra.h>
#include "global.h"
#include "soh/Enhancements/vr-combat/VrCombat.h"
#include "soh/Enhancements/vr-combat/VrHookshot.h"
#include "overlays/actors/ovl_Arms_Hook/z_arms_hook.h"
#include <vr_interface.h>

extern PlayState* gPlayState;

static f32 sVrHookRopeLen = 0.0f;   // current rope length
static s32 sVrHookTugging = false;  // a tug is in progress (one burst per tug)

// ---- Dual hookshots ("VR Dual Hookshots", under the grapple cheat). The first hook is the vanilla
// one in the item hand; the second (vrSecond) rides the sword hand. Each fires, holds and lets go
// on its own hand's trigger. Link's own rope state above follows whichever hook he hangs from.

static s32 sTriggerB = false;     // sword-hand trigger this tick
static s32 sTriggerBPrev = false; // ... last tick (fire on the squeeze edge)

s32 VrHookshot_DualActive(void) {
    Player* player;

    if ((gPlayState == NULL) || !Player_VrHookshotGrappleEnabled() ||
        !CVarGetInteger(CVAR_CHEAT("VrHookshotDual"), 0) || !CVarGetInteger("gVrMotionHands", 1)) {
        return false;
    }
    player = GET_PLAYER(gPlayState);
    return (player != NULL) && Player_HoldsHookshot(player);
}

s32 VrHookshot_HookHand(ArmsHook* hook) {
    return hook->vrSecond ? VrCombat_SwordHandIndex() : VrCombat_AimHandIndex();
}

s32 VrHookshot_TriggerHeld(ArmsHook* hook) {
    return hook->vrSecond ? sTriggerB : VrCombat_AimTriggerHeld();
}

// padmgr: while dual hookshots are out, the sword-hand trigger is the second hook's and nothing else's.
s32 VrHookshot_TriggerConsumed(s32 vrHand, u16 vrBtnMask) {
    return (vrBtnMask & VR_BTN_TRIGGER) && (vrHand == VrCombat_SwordHandIndex()) && VrHookshot_DualActive();
}

// The hook of the given kind (second or not) in this scene's actor list, or NULL. Looked up each
// time rather than kept, so a scene change can't leave a stale pointer.
ArmsHook* VrHookshot_FindHook(PlayState* play, s32 second) {
    Actor* actor = play->actorCtx.actorLists[ACTORCAT_ITEMACTION].head;

    for (; actor != NULL; actor = actor->next) {
        if ((actor->id == ACTOR_ARMS_HOOK) && (((ArmsHook*)actor)->vrSecond == second)) {
            return (ArmsHook*)actor;
        }
    }
    return NULL;
}

// Once per player update. Keeps the second hook in existence exactly while dual hookshots are out,
// fires it on the sword-hand trigger, and lets go of any hook whose trigger is released.
void VrHookshot_Tick(Player* player, PlayState* play) {
    ArmsHook* second = VrHookshot_FindHook(play, true);
    s32 i;

    sTriggerB = VR_IsInitialized() && VR_GetFirstPerson() &&
                (VR_GetGameButtons(VrCombat_SwordHandIndex()) & VR_BTN_TRIGGER);

    if (!VrHookshot_DualActive()) {
        if (second != NULL) {
            if (player->actor.parent == &second->actor) {
                player->actor.parent = NULL;
            }
            Actor_Kill(&second->actor);
        }
        sTriggerBPrev = sTriggerB;
        return;
    }

    if (second == NULL) {
        second = (ArmsHook*)Actor_Spawn(&play->actorCtx, play, ACTOR_ARMS_HOOK, player->actor.world.pos.x,
                                        player->actor.world.pos.y, player->actor.world.pos.z, 0,
                                        player->actor.shape.rot.y, 0, 0);
        if (second != NULL) {
            second->vrSecond = true;
            second->vrOrigin = player->actor.world.pos;
            second->actor.parent = &player->actor; // loaded: ArmsHook_Wait fires once this is cleared
        }
    }

    // Fire the second hook on a fresh squeeze, on the ground or in the air.
    if ((second != NULL) && sTriggerB && !sTriggerBPrev && ArmsHook_IsLoaded(second)) {
        LUSLOG_INFO("[VrHook] fire: second hook");
        second->actor.parent = NULL;
    }
    sTriggerBPrev = sTriggerB;

    // A hook holding a point that Link isn't hanging from lets go when its own trigger is released.
    // The one he hangs from is let go by the rope action (Player_VrHookshotLetGo).
    for (i = 0; i < 2; i++) {
        ArmsHook* hook = VrHookshot_FindHook(play, i);

        if ((hook != NULL) && (hook->actor.child == &player->actor) && (player->actor.parent != &hook->actor) &&
            !VrHookshot_TriggerHeld(hook)) {
            LUSLOG_INFO("[VrHook] let go: %s hook (not the one Link hangs from)", i ? "second" : "first");
            ArmsHook_ReleaseLine(hook, player);
            hook->actor.child = NULL;
        }
    }

    // Link let go of one hook while the other still holds: he swings on from that one.
    if (player->actor.parent == NULL) {
        for (i = 0; i < 2; i++) {
            ArmsHook* hook = VrHookshot_FindHook(play, i);

            if ((hook != NULL) && (hook->actor.child == &player->actor)) {
                player->actor.parent = &hook->actor;
                break;
            }
        }
    }
}

// Vanilla ends the hookshot flight when the hook is back in Link's hand (his actor.child). That is the
// first hook's state: while Link hangs from the second hook, the first one sitting loaded in the gun
// must not end his swing.
s32 VrHookshot_HangHookHome(Player* player) {
    Actor* hook = player->actor.parent;

    if ((hook != NULL) && (hook->id == ACTOR_ARMS_HOOK) && ((ArmsHook*)hook)->vrSecond) {
        return false;
    }
    return player->actor.child != NULL;
}

// The hook just caught: the rope starts at the distance from the gun barrel to the hook point.
void VrHookshot_OnCatch(Player* this) {
    sVrHookRopeLen = Math_Vec3f_DistXYZ(ArmsHook_Origin((ArmsHook*)this->actor.parent, this),
                                        &this->actor.parent->world.pos);
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

    ArmsHook* hook = (ArmsHook*)this->actor.parent;
    Vec3f* origin = ArmsHook_Origin(hook, this);

    dist = Math_Vec3f_DistXYZAndStoreDiff(origin, &this->actor.parent->world.pos, &n);
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

        VrItemThrow_HandVelocity(VrHookshot_HookHand(hook), &hand.x);
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
    Math_Vec3f_Sum(origin, vel, &next);
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
