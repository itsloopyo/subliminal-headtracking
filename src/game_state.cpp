// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "game_state.h"

#include <atomic>

#include "builds/build_registry.h"
#include "logging.h"
#include "ue_objects.h"
#include "ue_reflect.h"

#include "cameraunlock/unreal/ue_runtime.h"

namespace subliminal_ht::game_state {

namespace {

namespace ue = ::cameraunlock::unreal;

// AController::Pawn, resolved once off the live controller's class.
std::size_t g_pawnOffset = 0;
bool g_pawnResolved = false;
bool g_pawnFailed = false;

// APlayerController::bShowMouseCursor - the gameplay gate, and the one
// cross-check that says this profile's ReflectionLayout fits this build.
//
// The PROFILE is authoritative for the read, and deliberately so. The offset is
// derived from the exact binary the PE fingerprint routed to, and a bitfield
// needs two numbers, not one: FProperty::Offset_Internal gives the byte, and
// which BIT within it is an FBoolProperty ByteMask that this mod's reflection
// cannot read. Taking the engine's byte and the profile's bit would combine a
// number the engine confirmed with one it did not, and say nothing about it.
//
// So reflection is a diagnostic here, not a source. A disagreement means the
// property walk is following noise, and everything built on that walk - the aim
// trace's parameter frame, the lean sweep - is writing into wherever the noise
// pointed. It is reported loudly and once. It does NOT stand the gate down: a
// failed walk would then be indistinguishable from a menu, tracking would be off
// for the whole session, and the explanation would be one line that scrolled
// past at startup.
bool g_reflectionChecked = false;

void CheckReflectionLayout(std::uintptr_t controllerClass) {
    ue_reflect::FieldInfo cursor;
    if (!ue_reflect::FindPropertyInChain(controllerClass, "bShowMouseCursor", cursor)) {
        Log::Line("reflect-check: bShowMouseCursor is not in the controller's property "
                  "chain. The ReflectionLayout in this build profile does not fit this "
                  "game build, so the aim trace and the lean sweep will not resolve "
                  "either. The gameplay gate still reads the profile's own offset.");
        g_reflectionChecked = true;
        return;
    }
    const std::size_t expected = Offsets().Engine.kShowMouseCursorOffset;
    if (cursor.Offset != expected)
        Log::Line("reflect-check: bShowMouseCursor reflects at +0x%zx but this profile's "
                  "gameplay gate reads +0x%zx. Two derivations of one number disagree, so "
                  "this profile is wrong about at least one of them and every reflected "
                  "offset in it is suspect.", cursor.Offset, expected);
    else
        Log::Line("reflect-check: bShowMouseCursor reflects at +0x%zx, matching the "
                  "profile - the reflection layout fits this build", cursor.Offset);
    g_reflectionChecked = true;
}

bool ReadCursorFlag(std::uintptr_t controller, bool& cursorUp) {
    // The cross-check needs a class, which needs a guarded read that can fail on
    // an early frame. Only a run that produced a verdict burns the one shot.
    if (!g_reflectionChecked) {
        const std::uintptr_t cls = ue_objects::ClassOf(controller);
        if (cls) CheckReflectionLayout(cls);
    }
    const auto& engine = Offsets().Engine;
    std::uint32_t flags = 0;
    if (!ue::SafeReadU32(controller + engine.kShowMouseCursorOffset, flags))
        return false;
    cursorUp = (flags & engine.kShowMouseCursorMask) != 0;
    return true;
}

// The world the controller is playing in. A controller is an actor, so its
// outer is the level it was spawned into and the level's outer is the UWorld.
// Two guarded loads, no script VM, nothing to resolve - which is the whole
// point: the gate below has to work on a frame where reflection does not.
std::uintptr_t WorldOf(std::uintptr_t controller) {
    const std::uintptr_t level = ue::OuterObject(controller);
    if (!level) return 0;
    return ue::OuterObject(level);
}

// Whether the session has a net driver.
//
// Subliminal ships no multiplayer: the menu is Episode I / SETTINGS / EJECT,
// the episode screen has one BEGIN button, the live world runs the engine's own
// GameStateBase and GameSession with no networked subclass, and this field
// reads null throughout play. So this gate never fires in the shipping game,
// and it is here because it costs two pointer reads rather than because there
// is a mode to catch. A read that fails answers "networked", so a controller
// that is not the class the profile was derived against stands tracking down
// rather than running blind.
bool NetDriverPresent(std::uintptr_t controller, bool& known) {
    known = false;
    const std::uintptr_t world = WorldOf(controller);
    if (!world) return true;
    std::uintptr_t netDriver = 0;
    if (!ue::SafeReadPtr(world + Offsets().Engine.kWorldNetDriverOffset, netDriver))
        return true;
    known = true;
    return netDriver != 0;
}

}  // namespace

std::uintptr_t PossessedPawn(std::uintptr_t controller) {
    if (!g_pawnResolved) {
        if (g_pawnFailed) return 0;
        const std::uintptr_t cls = ue_objects::ClassOf(controller);
        if (!cls) return 0;
        ue_reflect::FieldInfo pawn;
        // Declared on AController, several classes above whatever Blueprint
        // subclass the game runs, so the search has to walk the SuperStruct
        // chain rather than the leaf class alone.
        if (!ue_reflect::FindPropertyInChain(cls, "Pawn", pawn)) {
            Log::Line("pawn: AController::Pawn is not in the controller's property "
                      "chain - the aim trace and the lean clamp both need it, and "
                      "will stay down");
            g_pawnFailed = true;
            return 0;
        }
        if (pawn.Size != sizeof(std::uintptr_t)) {
            Log::Line("pawn: AController::Pawn is %zu bytes, not a pointer - the aim "
                      "trace and the lean clamp will stay down", pawn.Size);
            g_pawnFailed = true;
            return 0;
        }
        // Two derivations of one number again: the profile carries the offset
        // read out of the EXE, the engine reports its own. A disagreement is
        // reported rather than absorbed, because the traces are handed this
        // pointer and a wrong one is an actor that is not the player.
        if (pawn.Offset != Offsets().Engine.kPawnOffset) {
            Log::Line("pawn: AController::Pawn reflects at +0x%zx but this profile says "
                      "+0x%zx - using the engine's", pawn.Offset,
                      Offsets().Engine.kPawnOffset);
        }
        g_pawnOffset = pawn.Offset;
        g_pawnResolved = true;
        Log::Line("pawn: AController::Pawn at +0x%zx", g_pawnOffset);
    }
    std::uintptr_t pawn = 0;
    if (!ue::SafeReadPtr(controller + g_pawnOffset, pawn)) return 0;
    return pawn;
}

Verdict Evaluate(std::uintptr_t controller) {
    Verdict v;

    // Gameplay gate. UE raises bShowMouseCursor exactly when input belongs to a
    // menu rather than the player, and `self` at the view hook IS the
    // controller, so this is one guarded load off a pointer already in hand -
    // no sampling thread, and no confusion with a cursor some other window put
    // up. An unreadable flag means the controller is not the class this profile
    // was derived against, which reports "not in gameplay".
    bool cursorUp = true;
    v.InGameplay = ReadCursorFlag(controller, cursorUp) && !cursorUp;

    bool known = false;
    v.Offline = !NetDriverPresent(controller, known);
    v.NetStateKnown = known;
    return v;
}

void LogTransitions(const Verdict& v) {
    static std::atomic<int> s_lastGameplay{-1};
    static std::atomic<int> s_lastNet{-1};

    const int gameplay = v.InGameplay ? 1 : 0;
    if (s_lastGameplay.exchange(gameplay) != gameplay)
        Log::Line("gate: %s", gameplay ? "gameplay resumed"
                                       : "menu/loading/paused - tracking suspended");

    const int net = !v.NetStateKnown ? 2 : (v.Offline ? 0 : 1);
    if (s_lastNet.exchange(net) != net) {
        switch (net) {
        case 0:
            Log::Line("gate: offline session (no net driver) - head tracking enabled");
            break;
        case 1:
            Log::Line("gate: the world has a net driver - head tracking DISABLED");
            break;
        default:
            Log::Line("gate: the world's net driver could not be read - head tracking "
                      "DISABLED until it can be");
            break;
        }
    }
}

}  // namespace subliminal_ht::game_state
