extern "C" {
#include "z64.h"
#include "macros.h"
#include "functions.h"
#include "variables.h" // gMtxClear
#include "overlays/actors/ovl_En_Arrow/z_en_arrow.h" // ARROW_NUT
extern PlayState* gPlayState;
}
#include "VrCombat.h"
#include "VrEmittedMatrices.h"
#include <libultraship/bridge/consolevariablebridge.h>
#include <cmath>
#include <cstring>
#include <vr_interface.h>

namespace {
bool sGripPrev[2] = { true, true }; // require a fresh press on entry/reset
int sCarryHand = -1;                // hand that grabbed the current throwable, -1 = none
int sObservedItem = -1;
bool sReleaseHasGrip = false;

float WorldScale() {
    const float scale = VR_GetWorldScale();
    return scale < 1.0f ? 35.0f : scale;
}

int SwordHandIdx() {
    return CVarGetInteger("gVrLeftHanded", 0) ? VR_HAND_LEFT : VR_HAND_RIGHT;
}

// The hand that owns the current carry. A hold restored without an observed grab (save-state
// load) defaults to the sword hand; sReleaseHasGrip stays false there, so it drops, not throws.
int CarryHand() {
    return sCarryHand >= 0 ? sCarryHand : SwordHandIdx();
}

bool HasThrowable(Player* player) {
    return player && player->heldActor && (player->stateFlags1 & PLAYER_STATE1_CARRYING_ACTOR) &&
           player->heldActor->parent == &player->actor &&
           (player->heldActor->id == ACTOR_EN_BOM || player->heldActor->id == ACTOR_EN_ARROW);
}

bool SwapChord() {
    const auto mask = (uint16_t)CVarGetInteger("gVrItemSelSwapInput", VR_BTN_GRIP);
    return mask && (VR_GetGameButtons(0) & mask) && (VR_GetGameButtons(1) & mask);
}

// Hand reach, shared by the item preview and world objects. A hand is within reach when it is
// no farther than `reach` from a vertical cylinder (radius, height above `base`); a point is the
// zero-size cylinder. Returns the squared distance through outDistSq when in reach.
bool HandNearCylinder(int hand, const float* base, float radius, float height, float reach,
                      float* outDistSq = nullptr) {
    float position[3], rotation[4];
    if (!VR_GetHandPose(hand, position, rotation)) {
        return false;
    }
    const float dx = position[0] - base[0];
    const float dz = position[2] - base[2];
    const float horizontal = std::sqrt(dx * dx + dz * dz);
    const float outside = horizontal > radius ? horizontal - radius : 0.0f;
    const float dy = position[1] < base[1]            ? base[1] - position[1]
                     : position[1] > base[1] + height ? position[1] - (base[1] + height)
                                                      : 0.0f;
    const float distanceSq = outside * outside + dy * dy;
    if (distanceSq > reach * reach) {
        return false;
    }
    if (outDistSq) *outDistSq = distanceSq;
    return true;
}

// Any-hand grabs (behavior plan): a hand is "about to grab" when it is within reach of the
// presented preview. Shared by the grab tick and the padmgr grip reservation, so a grip press
// only loses its normal binding when it would actually grab.
bool HandNearPreview(int hand) {
    float preview[3];
    return VrItemThrow_PreviewPosition(preview) &&
           HandNearCylinder(hand, preview, 0.0f, 0.0f, WorldScale() * 0.15f);
}

// ---- Physical lifting of world objects (pots, crates, small rocks, bushes, bomb flowers) ----
// Each object still decides for itself when it can be lifted: it offers itself through
// Actor_OfferGetItem(GI_NONE) exactly as in vanilla, and that offer passes Link's state gates
// first. Only the "is Link close and facing it" part is replaced by "is a hand on it".
// The grab cylinders are the objects' own collision cylinders (their sCylinderInit sizes).
bool LiftCylinder(Actor* actor, float* radius, float* height) {
    switch (actor->id) {
        case ACTOR_OBJ_TSUBO:  *radius = 9.0f;  *height = 26.0f; return true;
        case ACTOR_OBJ_KIBAKO: *radius = 12.0f; *height = 27.0f; return true;
        case ACTOR_EN_KUSA:    *radius = 12.0f; *height = 44.0f; return true;
        case ACTOR_EN_BOMBF:   *radius = 9.0f;  *height = 18.0f; return true;
        case ACTOR_EN_ISHI: // small rocks only; silver boulders keep the vanilla lift
            if ((actor->params & 1) != 0) return false;
            *radius = 10.0f; *height = 18.0f; return true;
        default: return false; // Cucco, Ruto, Master Sword and the rest stay vanilla
    }
}

Actor* sLiftCandidate = nullptr; // nearest offered object within a hand's reach, this frame
int sLiftCandidateHand = -1;
float sLiftCandidateDistSq = 0.0f;
int sLiftNearHand = -1;          // hand on a liftable object as of the last lift tick, -1 = none
u32 sLiftNearFrame = 0;          // gameplayFrames of that tick

// The near-hand result only counts while lift ticks are running; actions that skip
// Player_UpdateItems (cutscenes, talking) must not keep a grip reserved.
int LiftNearHand() {
    return (gPlayState && gPlayState->gameplayFrames - sLiftNearFrame <= 1) ? sLiftNearHand : -1;
}
int sLiftHand = -1;              // hand holding a physically lifted object, -1 = none
bool sLiftReleaseHasGrip = false;

// The candidate pointer is only trusted while the actor is still in the actor lists. Lift ticks
// can skip frames (Player_UpdateItems doesn't run in every action), so the candidate may be
// older than one frame by the time it is read.
bool ActorStillListed(PlayState* play, Actor* target) {
    for (int category = 0; category < ACTORCAT_MAX; ++category) {
        for (Actor* actor = play->actorCtx.actorLists[category].head; actor != NULL; actor = actor->next) {
            if (actor == target) return true;
        }
    }
    return false;
}

float LiftReach() {
    return WorldScale() * CVarGetFloat("gVrLiftReach", 15.0f) * 0.01f; // cm -> m -> game units
}

bool IsLiftCarry(Player* player) {
    float radius, height;
    return sLiftHand >= 0 && player && player->heldActor &&
           (player->stateFlags1 & PLAYER_STATE1_CARRYING_ACTOR) && player->heldActor->parent == &player->actor &&
           LiftCylinder(player->heldActor, &radius, &height);
}

// Hand velocity for a release: the shared headset-rate path snapshot (last 100 ms), else the
// runtime velocity. Physical m/s, game-facing frame.
void ReleaseVelocity(int hand, float* velocity) {
    float angular[3];
    velocity[0] = velocity[1] = velocity[2] = 0.0f;
    const auto& path = VrCombat::GetTickPath(hand);
    int samples = 0;
    if (path.count) {
        const auto latest = path.samples[path.count - 1].timeNs;
        for (int i = 0; i < path.count; ++i) {
            if (latest - path.samples[i].timeNs > 100000000ULL) continue;
            for (int axis = 0; axis < 3; ++axis) velocity[axis] += path.samples[i].linVelMps[axis];
            ++samples;
        }
    }
    if (samples) {
        for (int axis = 0; axis < 3; ++axis) velocity[axis] /= samples;
    } else {
        VR_GetHandVelocity(hand, velocity, angular);
    }
}
}

extern "C" bool VrItemThrow_LiftActive(Player* player) {
    return VR_IsInitialized() && VR_GetFirstPerson() && CVarGetInteger("gVrMotionHands", 1) &&
           CVarGetInteger("gVrPhysicalLift", 1) && player && player->actor.category == ACTORCAT_PLAYER &&
           gPlayState && gPlayState->bombchuBowlingStatus == 0 && gPlayState->shootingGalleryStatus == 0 &&
           !(player->stateFlags1 & (PLAYER_STATE1_ON_HORSE | PLAYER_STATE1_IN_WATER));
}

extern "C" bool VrItemThrow_LiftCarrying(Player* player) {
    return VrItemThrow_LiftActive(player) && IsLiftCarry(player);
}

// A hand is on a liftable object or holding one: the two-grip sword & shield swap stands down,
// so grabbing with one grip while the other is squeezed never also draws the sword.
extern "C" bool VrItemThrow_LiftEngaged(void) {
    if (!gPlayState) return false;
    Player* player = GET_PLAYER(gPlayState);
    return VrItemThrow_LiftActive(player) &&
           (IsLiftCarry(player) || (player->heldActor == NULL && LiftNearHand() >= 0));
}

// Called from Actor_OfferGetItem for GI_NONE offers, after Link's state gates passed.
extern "C" void VrItemThrow_OfferLift(Actor* actor) {
    float radius, height;
    if (!gPlayState || !actor || !LiftCylinder(actor, &radius, &height)) return;
    Player* player = GET_PLAYER(gPlayState);
    if (!VrItemThrow_LiftActive(player) || player->heldActor != NULL) return;
    const float base[3] = { actor->world.pos.x, actor->world.pos.y, actor->world.pos.z };
    for (int hand = 0; hand < 2; ++hand) {
        float distanceSq;
        if (HandNearCylinder(hand, base, radius, height, LiftReach(), &distanceSq) &&
            (sLiftCandidate == nullptr || distanceSq < sLiftCandidateDistSq)) {
            sLiftCandidate = actor;
            sLiftCandidateHand = hand;
            sLiftCandidateDistSq = distanceSq;
        }
    }
}

extern "C" bool VrItemThrow_Active(Player* player) {
    return VrItemSelect_ModeActive() && CVarGetInteger("gVrPhysicalItemThrows", 1) && player &&
           player->actor.category == ACTORCAT_PLAYER &&
           (player->heldItemAction == PLAYER_IA_BOMB || player->heldItemAction == PLAYER_IA_DEKU_NUT) &&
           gPlayState && gPlayState->bombchuBowlingStatus == 0 && gPlayState->shootingGalleryStatus == 0 &&
           !(player->stateFlags1 & (PLAYER_STATE1_ON_HORSE | PLAYER_STATE1_IN_WATER));
}

extern "C" void VrItemThrow_Reset(void) {
    sGripPrev[0] = sGripPrev[1] = true;
    sCarryHand = -1;
    sObservedItem = -1;
    sReleaseHasGrip = false;
}

extern "C" bool VrItemThrow_PreviewPosition(float* position) {
    if (!gPlayState || !VrItemThrow_Active(GET_PLAYER(gPlayState)) ||
        !VrItemSelect_SelectionAllowed() || GET_PLAYER(gPlayState)->heldActor) {
        return false;
    }
    float eye[3], forward[3], up[3];
    VR_GetCameraPose(eye, forward, up);
    const float scale = WorldScale();
    // Stable chest-height presentation in front of the player; looking up/down must
    // not move the target out of reach. Exact distances remain headset-tunable.
    float length = std::sqrt(forward[0] * forward[0] + forward[2] * forward[2]);
    if (length < 0.01f) return false;
    position[0] = eye[0] + forward[0] / length * scale * 0.4f;
    position[1] = eye[1] - scale * 0.25f;
    position[2] = eye[2] + forward[2] / length * scale * 0.4f;
    return true;
}

extern "C" bool VrItemThrow_GripConsumed(int32_t hand, uint16_t mask) {
    if (!gPlayState || !(mask & VR_BTN_GRIP) || VrOcarina_InPlay()) {
        return false;
    }
    Player* player = GET_PLAYER(gPlayState);
    // Physical lift: the holding hand's grip is the release input; with empty hands, only a
    // hand that is on a liftable object loses its normal binding.
    if (VrItemThrow_LiftActive(player)) {
        if (IsLiftCarry(player)) return hand == sLiftHand;
        if (player->heldActor == NULL && hand == LiftNearHand()) return true;
    }
    if (!VrItemThrow_Active(player)) {
        return false;
    }
    // Carrying: the owning hand's grip is the release input. Preview up: only a hand close
    // enough to grab loses its binding — a grip press elsewhere keeps Z-target and friends.
    return HasThrowable(player) ? hand == CarryHand() : HandNearPreview(hand);
}

extern "C" void VrItemThrow_UpdateCarryPose(Player* player) {
    float position[3], rotation[4];
    if (VrItemThrow_LiftCarrying(player)) {
        // One-hand hold: the object's base sits just under the hand, centered on it.
        float radius, height;
        if (VR_GetHandPose(sLiftHand, position, rotation) && LiftCylinder(player->heldActor, &radius, &height)) {
            player->heldActor->world.pos = { position[0], position[1] - height * 0.5f, position[2] };
        }
        return;
    }
    if (!VrItemThrow_Active(player) || !HasThrowable(player)) return;
    if (VR_GetHandPose(CarryHand(), position, rotation)) {
        player->heldActor->world.pos = { position[0], position[1], position[2] };
    }
}

// Grab and release of world objects. Runs every Player_UpdateItems, before the item path.
extern "C" void VrItemThrow_LiftTick(PlayState* play, Player* player) {
    // Offers are collected after Link updates (props update later in the frame), so the
    // candidate comes from an earlier frame: check it still exists and hasn't been killed.
    Actor* candidate = sLiftCandidate;
    const int candidateHand = sLiftCandidateHand;
    sLiftCandidate = nullptr;
    sLiftCandidateHand = -1;
    if (candidate != nullptr && (!ActorStillListed(play, candidate) || candidate->update == NULL)) {
        candidate = nullptr;
    }
    sLiftNearHand = candidate != nullptr ? candidateHand : -1;
    sLiftNearFrame = play->gameplayFrames;

    static bool sLiftGripPrev[2] = { true, true };
    bool pressed[2], released[2];
    for (int hand = 0; hand < 2; ++hand) {
        const bool grip = (VR_GetGameButtons(hand) & VR_BTN_GRIP) != 0;
        pressed[hand] = grip && !sLiftGripPrev[hand];
        released[hand] = !grip && sLiftGripPrev[hand];
        sLiftGripPrev[hand] = grip;
    }

    if (!VrItemThrow_LiftActive(player) || play->pauseCtx.state != 0) {
        // Leaving first person (or VR) while holding: vanilla carry takes over (A throws).
        sLiftHand = -1;
        sLiftNearHand = -1;
        return;
    }
    if (IsLiftCarry(player)) {
        if (VR_GetGameButtons(sLiftHand) & VR_BTN_GRIP) sLiftReleaseHasGrip = true;
        VrItemThrow_UpdateCarryPose(player);
        if (released[sLiftHand]) {
            float velocity[3];
            ReleaseVelocity(sLiftHand, velocity);
            // A hold restored without an observed grip is a drop, never a throw.
            if (!sLiftReleaseHasGrip) velocity[0] = velocity[1] = velocity[2] = 0.0f;
            const float scale = WorldScale();
            const float strength = CVarGetFloat("gVrLiftThrowStrength", 1.0f);
            for (float& component : velocity) component *= scale * 1.4f / 20.0f * strength;
            VR_TriggerHaptic(sLiftHand, 0.35f, 0.0f, 25.0f);
            sLiftHand = -1;
            sLiftReleaseHasGrip = false;
            Player_VrReleaseLifted(play, player, velocity);
        }
        return;
    }
    sLiftHand = -1;
    if (candidate != nullptr && candidateHand >= 0 && pressed[candidateHand] && player->heldActor == NULL &&
        Player_VrLiftActor(play, player, candidate)) {
        sLiftHand = candidateHand;
        sLiftReleaseHasGrip = true;
        VrItemThrow_UpdateCarryPose(player);
        VR_TriggerHaptic(candidateHand, 0.5f, 0.0f, 35.0f);
    }
}

extern "C" void VrItemThrow_Tick(PlayState* play, Player* player) {
    if (!VrItemThrow_Active(player) || !VrItemSelect_SelectionAllowed() ||
        play->pauseCtx.state != 0 || player->unk_6AD != 0 || player->heldItemAction != player->itemAction) {
        VrItemThrow_Reset();
        return;
    }
    if (sObservedItem != player->heldItemId) {
        sObservedItem = player->heldItemId;
        sGripPrev[0] = sGripPrev[1] = true;
    }
    bool pressed[2], released[2];
    for (int hand = 0; hand < 2; ++hand) {
        const bool grip = (VR_GetGameButtons(hand) & VR_BTN_GRIP) != 0;
        pressed[hand] = grip && !sGripPrev[hand];
        released[hand] = !grip && sGripPrev[hand];
        sGripPrev[hand] = grip;
    }
    if (HasThrowable(player) && (VR_GetGameButtons(CarryHand()) & VR_BTN_GRIP)) {
        sReleaseHasGrip = true;
    }
    if (SwapChord() || VrItemSelect_PendingSlot() != -2) return;

    if (HasThrowable(player)) {
        VrItemThrow_UpdateCarryPose(player);
        const int hand = CarryHand();
        if (released[hand]) {
            float velocity[3] = {}, angular[3];
            // Runtime velocities exclude locomotion/snap-turn displacement. Use the
            // shared path snapshot if available, never drain the XR buffer again.
            const auto& path = VrCombat::GetTickPath(hand);
            int samples = 0;
            if (path.count) {
                const auto latest = path.samples[path.count - 1].timeNs;
                for (int i = 0; i < path.count; ++i) {
                    if (latest - path.samples[i].timeNs > 100000000ULL) continue;
                    for (int axis = 0; axis < 3; ++axis) velocity[axis] += path.samples[i].linVelMps[axis];
                    ++samples;
                }
            }
            if (samples) {
                for (float& component : velocity) component /= samples;
            } else {
                VR_GetHandVelocity(hand, velocity, angular);
            }
            // A restored/interrupted hold without an observed grip is a drop, never
            // a throw using motion samples from the previous timeline.
            if (!sReleaseHasGrip) velocity[0] = velocity[1] = velocity[2] = 0.0f;
            sReleaseHasGrip = false;
            const float scale = WorldScale();
            for (float& component : velocity) component *= scale * 1.4f / 20.0f;
            Player_VrReleaseItem(play, player, velocity);
            VR_TriggerHaptic(hand, 0.35f, 0.0f, 25.0f);
            sCarryHand = -1;
        }
    } else {
        sCarryHand = -1;
        // Any hand may claim the preview; sword hand checked first on a same-tick tie.
        const int first = SwordHandIdx();
        for (int i = 0; i < 2; ++i) {
            const int hand = i == 0 ? first : 1 - first;
            if (pressed[hand] && HandNearPreview(hand) && Player_VrGrabItem(play, player)) {
                sCarryHand = hand;
                VrItemThrow_UpdateCarryPose(player);
                VR_TriggerHaptic(hand, 0.5f, 0.0f, 35.0f);
                break;
            }
        }
    }
}

// ---- QuestShip: the Deku Nut as a real 3D nut in VR ---- (slingshot seeds stay vanilla)
// Vanilla has no nut model for the projectile: in flight it is a near-black spinning sparkle
// (EnArrow_Draw, ARROW_NUT), and while carried it draws nothing (that path needs speedXZ != 0).
// In VR the nut is the get-item model instead: welded to the carrying hand at headset rate
// (live hand-child matrices, like the bowstring), and tumbling along its flight. Display-only:
// masked out of physical combat's visual-mesh collision.
namespace {
// Re-express every model matrix just emitted relative to the hand at this tick and register it as
// a live child of that hand.
void WeldEmittedToHand(Gfx* from, Gfx* to, int hand, MtxF* handInv) {
    VrCombat::ForEachEmittedLoadMatrix(from, to, gPlayState ? gPlayState->billboardMtx : NULL, [&](Mtx* m, MtxF& cur) {
        MtxF local;
        SkinMatrix_MtxFMtxFMult(handInv, &cur, &local);
        VR_RegisterHandChildMatrix((const void*)m, hand, &local.mf[0][0]);
    });
}
} // namespace

extern "C" bool VrItemThrow_DrawNutModel(Actor* actor, PlayState* play) {
    if (actor == NULL || play == NULL || actor->params != ARROW_NUT ||
        !VR_IsInitialized() || !CVarGetInteger("gVrNutModel", 1)) {
        return false;
    }
    const s16 gid = GID_NUTS;
    Player* player = GET_PLAYER(play);
    const bool held = player != NULL && player->heldActor == actor && actor->parent == &player->actor;
    const bool flying = actor->parent == NULL && (actor->speedXZ != 0.0f || actor->velocity.y != 0.0f);
    if (!held && !flying) {
        return false;
    }
    const float scale = CVarGetFloat("gVrNutModelScale", 0.06f);

    OPEN_DISPS(play->state.gfxCtx);
    VrCombat_MeshMaskPush(play->state.gfxCtx);
    Gfx* opaStart = POLY_OPA_DISP;
    Gfx* xluStart = POLY_XLU_DISP;
    if (held) {
        const int hand = CarryHand();
        // Start from the hand's full matrix (position AND orientation), scaled to nut size, so the
        // nut is rigidly in the grip. (Position-only made it snap back to a neutral orientation
        // at every 20 Hz tick, only turning with the hand in between.)
        float hm[4][4];
        if (VR_GetHandMatrix(hand, hm)) {
            MtxF handMtx;
            MtxF handInv;
            memcpy(handMtx.mf, hm, sizeof(handMtx.mf));
            // The hand matrix carries Link's model scale; normalize it out of the nut's size.
            const float handScale = sqrtf(hm[0][0] * hm[0][0] + hm[0][1] * hm[0][1] + hm[0][2] * hm[0][2]);
            const float rel = handScale > 1e-6f ? scale / handScale : scale;
            // At the hand frame's origin: the grip point, where the empty hand's palm is drawn
            // (the VR layer centers empty-hand meshes on the grip).
            Matrix_Put(&handMtx);
            // A mirrored hand (the sword hand) carries a reflection; undo it for the nut, or its
            // triangles wind backwards and it renders inside-out.
            const float det = hm[0][0] * (hm[1][1] * hm[2][2] - hm[1][2] * hm[2][1]) -
                              hm[0][1] * (hm[1][0] * hm[2][2] - hm[1][2] * hm[2][0]) +
                              hm[0][2] * (hm[1][0] * hm[2][1] - hm[1][1] * hm[2][0]);
            const float mirrorZ = det < 0.0f ? -1.0f : 1.0f;
            Matrix_Scale(rel, rel, rel * mirrorZ, MTXMODE_APPLY);
            GetItem_Draw(play, gid);
            if (SkinMatrix_Invert(&handMtx, &handInv) == 0) {
                WeldEmittedToHand(opaStart, POLY_OPA_DISP, hand, &handInv);
                WeldEmittedToHand(xluStart, POLY_XLU_DISP, hand, &handInv);
            }
        } else {
            Matrix_Translate(actor->world.pos.x, actor->world.pos.y, actor->world.pos.z, MTXMODE_NEW);
            Matrix_Scale(scale, scale, scale, MTXMODE_APPLY);
            GetItem_Draw(play, gid);
        }
    } else {
        const float spin = BINANG_TO_RAD((s16)(play->gameplayFrames * 4000)); // wraps smoothly
        Matrix_Translate(actor->world.pos.x, actor->world.pos.y, actor->world.pos.z, MTXMODE_NEW);
        Matrix_RotateY(spin, MTXMODE_APPLY);
        Matrix_RotateX(spin * 0.7f, MTXMODE_APPLY);
        Matrix_Scale(scale, scale, scale, MTXMODE_APPLY);
        GetItem_Draw(play, gid);
    }
    VrCombat_MeshMaskPop(play->state.gfxCtx);
    CLOSE_DISPS(play->state.gfxCtx);
    return true;
}
