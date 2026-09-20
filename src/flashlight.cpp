// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "flashlight.h"

#include <cstddef>

#include "logging.h"
#include "ue_objects.h"
#include "ue_reflect.h"

#include "cameraunlock/unreal/ue_runtime.h"

namespace subliminal_ht::flashlight {

namespace {

namespace ue = ::cameraunlock::unreal;

// The character's spot light, by the name the blueprint gives it.
constexpr const char* kSpotProperty = "FlashlightLightSpot";

// An FRotator and an FVector are both three doubles in declaration order, and
// USceneComponent stores RelativeRotation as Pitch, Yaw, Roll - so the vector
// writer moves a rotator correctly. Spelled out here rather than cast, because
// the one thing that would make it wrong is a field that is not 24 bytes, and
// that is checked before either write.
constexpr std::size_t kThreeDoubles = 3 * sizeof(double);

std::uintptr_t g_pawnClass = 0;
std::size_t    g_spotOffset = 0;
bool           g_pawnResolveFailed = false;

std::uintptr_t g_componentClass = 0;
std::size_t    g_relLocOffset = 0;
std::size_t    g_relRotOffset = 0;
bool           g_componentResolveFailed = false;

// The component currently being driven, with the transform it had before this
// mod first touched it. Keyed by pointer: a reloaded level builds a new light,
// and restoring the old one's numbers onto it would be writing a stale pose.
std::uintptr_t g_held = 0;
ue::FVector    g_originalLoc{};
ue::FVector    g_originalRot{};
bool           g_announced = false;

void GiveUp(bool& latch, const char* what) {
    if (latch) return;
    latch = true;
    Log::Line("flashlight: %s, so the beam stays on the mouse aim.", what);
}

// The pawn's FlashlightLightSpot, or 0 when this character has no such member
// or is not carrying a light yet.
//
// The offset is held against the class it came from rather than latched:
// FindPropertyInChain walks the whole SuperStruct chain building two heap
// strings per field, which is not something to do on every rendered frame.
std::uintptr_t ResolveSpot(std::uintptr_t pawn) {
    if (!pawn || g_pawnResolveFailed) return 0;

    const std::uintptr_t pawnClass = ue_objects::ClassOf(pawn);
    if (!pawnClass) return 0;

    if (pawnClass != g_pawnClass) {
        ue_reflect::FieldInfo spot;
        if (!ue_reflect::FindPropertyInChain(pawnClass, kSpotProperty, spot)) {
            GiveUp(g_pawnResolveFailed,
                   "the character has no FlashlightLightSpot member");
            return 0;
        }
        if (spot.TypeName != "ObjectProperty" || spot.Size != sizeof(std::uintptr_t)) {
            GiveUp(g_pawnResolveFailed,
                   "FlashlightLightSpot is not a pointer-sized object property");
            return 0;
        }
        g_spotOffset = spot.Offset;
        g_pawnClass = pawnClass;
    }

    std::uintptr_t spot = 0;
    if (!ue::SafeReadPtr(pawn + g_spotOffset, spot)) return 0;
    return spot;
}

// RelativeLocation and RelativeRotation on whatever component class the light
// turns out to be.
//
// Both are declared on USceneComponent, several classes above any light, so
// this walks the SuperStruct chain - ResolveAll searches the leaf class alone
// and would find neither. Each is checked for the 24 bytes its write needs: a
// build whose LWC layout made them floats would otherwise take a double-width
// write into the fields behind them.
bool ResolveTransformFields(std::uintptr_t spot) {
    if (g_componentResolveFailed) return false;

    const std::uintptr_t componentClass = ue_objects::ClassOf(spot);
    if (!componentClass) return false;
    if (componentClass == g_componentClass) return true;

    ue_reflect::FieldInfo loc, rot;
    if (!ue_reflect::FindPropertyInChain(componentClass, "RelativeLocation", loc) ||
        !ue_reflect::FindPropertyInChain(componentClass, "RelativeRotation", rot)) {
        GiveUp(g_componentResolveFailed,
               "the light's relative transform is not in its property chain");
        return false;
    }
    for (const ue_reflect::FieldInfo& f : {loc, rot}) {
        if (f.TypeName != "StructProperty" || f.Size != kThreeDoubles) {
            GiveUp(g_componentResolveFailed,
                   "the light's relative transform is not a pair of three-double "
                   "structs - this build's LWC layout is not the one the mod writes");
            return false;
        }
    }
    g_relLocOffset = loc.Offset;
    g_relRotOffset = rot.Offset;
    g_componentClass = componentClass;
    return true;
}

// Remember what the game had, once per light. Returns false when the transform
// cannot be read, which is the one case where writing would leave nothing to
// restore.
bool Take(std::uintptr_t spot) {
    if (g_held == spot) return true;
    if (!ue::SafeReadFVector(spot + g_relLocOffset, g_originalLoc)) return false;
    if (!ue::SafeReadFVector(spot + g_relRotOffset, g_originalRot)) return false;
    g_held = spot;
    if (!g_announced) {
        g_announced = true;
        Log::Line("flashlight: the beam follows the head now "
                  "(light at +0x%zx on the character, relative transform at "
                  "+0x%zx / +0x%zx)",
                  g_spotOffset, g_relLocOffset, g_relRotOffset);
    }
    return true;
}

}  // namespace

void Follow(std::uintptr_t pawn, const ue::FQuat4d& cleanQ, const ue::FQuat4d& beamQ,
            const ue::FVector& leanWorld) {
    const std::uintptr_t spot = ResolveSpot(pawn);
    if (!spot) return;
    if (!ResolveTransformFields(spot)) return;
    if (!Take(spot)) return;

    // The engine composes a child as world = parent * relative, and the parent
    // here is a socket built from the clean control rotation. So the relative
    // rotation that lands the beam where the caller wants it is the delta
    // between the two.
    const ue::FRotator rel = ue::QuatToRotator(ue::QuatMul(ue::QuatInv(cleanQ), beamQ));

    // Same argument for the origin. The lean moves the rendered eye in world
    // space; the socket sits at the clean one, so the offset goes in as the
    // clean camera sees it.
    const ue::FVector localLean = ue::QuatRotateVec(ue::QuatInv(cleanQ), leanWorld);

    ue::SafeWriteFVector(spot + g_relRotOffset,
                         ue::FVector{rel.Pitch, rel.Yaw, rel.Roll});
    ue::SafeWriteFVector(spot + g_relLocOffset, localLean);
}

void Release() {
    if (!g_held) return;
    ue::SafeWriteFVector(g_held + g_relRotOffset, g_originalRot);
    ue::SafeWriteFVector(g_held + g_relLocOffset, g_originalLoc);
    g_held = 0;
}

}  // namespace subliminal_ht::flashlight
