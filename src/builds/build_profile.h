// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

#include <cameraunlock/memory/pe_fingerprint.h>
#include <cameraunlock/unreal/ue_runtime.h>

// One BuildProfile describes a single shipped build of Subliminal: the
// PE-header fingerprint that uniquely identifies it, plus every per-build RVA
// and field offset the mod needs. The registry holds one profile per supported
// build; at startup the mod fingerprints the live module and selects the
// matching profile. No match leaves the mod fully dormant (no hooks installed,
// game runs vanilla) - see AGENTS.md "Maintain compatibility across new
// patches": never edit an existing profile's RVAs in place, ADD a new one.

namespace subliminal_ht {

// PE-header build fingerprint (TimeDateStamp + SizeOfImage + CheckSum);
// the shared type keeps reading/matching/classification in core.
using PeFingerprint = ::cameraunlock::memory::PeFingerprint;

// Where the engine keeps the reflection data this mod reads: a UFunction's
// parameter list and a UScriptStruct's fields, by name.
//
// These are engine-version-bound rather than build-bound, and they are not
// the numbers the older UE mods in the fleet carry. UE 5.7 stores
// FField::Owner as a single tagged pointer rather than a pointer plus a
// bool, so everything after it in FField sits eight bytes lower.
struct ReflectionLayout {
    std::size_t kFField_ClassPrivate;      // FFieldClass* (the property's type)
    std::size_t kFField_Next;              // next FField in the linked list
    std::size_t kFField_NamePrivate;       // FName of the property
    std::size_t kFProperty_ArrayDim;
    std::size_t kFProperty_ElementSize;
    std::size_t kFProperty_Offset;         // Offset_Internal
    std::size_t kFFieldClass_Name;         // FName at the head of FFieldClass
    std::size_t kUStruct_SuperStruct;
    std::size_t kUStruct_ChildProperties;  // head of the FField list
    std::size_t kUStruct_PropertiesSize;
};

// The engine-side struct offsets the mod reads off live objects. Every one
// was read out of the engine's own UECodeGen property tables rather than
// guessed, and each is checked against the live class before use.
struct EngineOffsets {
    // APlayerController::bShowMouseCursor, as a byte offset into the
    // controller plus the bit within the dword there. UE raises that flag
    // exactly when input belongs to a menu rather than the player, so it is
    // the gameplay gate: cursor up -> suppress tracking. The hook already
    // holds the controller pointer, so reading it costs one load.
    std::size_t   kShowMouseCursorOffset;
    std::uint32_t kShowMouseCursorMask;

    // AController::Pawn. The pawn is what the collision sweep is handed as
    // its world context with bIgnoreSelf set, which is what stops the sweep
    // reporting the player's own capsule at zero distance every frame.
    std::size_t   kPawnOffset;

    // UWorld::NetDriver, reached through the controller's outer chain
    // (controller -> PersistentLevel -> UWorld). Non-null means the session
    // is networked and the mod stands down. Subliminal ships no multiplayer
    // mode, so this never fires in the shipping game; it costs one guarded
    // load and it is what makes that claim safe rather than assumed.
    std::size_t   kWorldNetDriverOffset;
};

struct OffsetTable {
    // Hook target: APlayerController::GetPlayerViewPoint. RVA from the
    // module base. Zero = profile incomplete (mod stays dormant).
    std::uintptr_t kGetPlayerViewPointRva;

    // Return-address RVAs of the distinct GetPlayerViewPoint call sites.
    // Head tracking is injected ONLY for callers flagged here per the
    // active inject mode; every other caller reads the clean (mouse/pad)
    // rotation. That per-caller gate IS the look/aim decoupling.
    // 0-valued trailing entries are unused padding.
    std::array<std::uintptr_t, 16> kKnownCallerRvas;

    // Default inject mode at startup. 0 = all callers (diagnostic only),
    // 1..16 = inject only for kKnownCallerRvas[mode-1] (the render-path
    // caller / FMinimalViewInfo builder), 17 = none. [Dev] InjectNextKey
    // and InjectPreviousKey step it live so the render caller can be
    // re-confirmed in game after a patch without a rebuild.
    int kDefaultInjectMode;

    // FMinimalViewInfo field offsets. The render caller is
    // ULocalPlayer::GetViewPoint, which hands GPV pointers to the Location
    // and Rotation fields of the FMinimalViewInfo it is filling in, so the
    // live FOV the frame will render with sits at outLocation + kFovOffset.
    // kRotationStride is the Location->Rotation gap, checked against the
    // actual out-param pair before the FOV is read - if the two pointers
    // are not that far apart they are not fields of one FMinimalViewInfo,
    // and what looks like the FOV is a local in some other caller's frame.
    struct {
        std::size_t kFovOffset;
        std::size_t kRotationStride;
    } MinimalViewInfoLayout;

    // GUObjectArray / FNamePool, for finding the reticle widget and the
    // Kismet trace functions by name. The shared core type is what
    // ue::SetRuntime consumes.
    ::cameraunlock::unreal::UObjectGlobalsLayout UObjectGlobals;

    // Byte offset of UObjectBase* within one FUObjectItem.
    //
    // UE 5.7 does not put the object pointer first: the item is 24 bytes and
    // the pointer sits eight in, behind the flags/serial fields. Core's
    // ForEachUObject reads it at item+0 and so cannot walk this build's
    // table, which is why src/ue_objects.h carries the walk and this field
    // drives it. Verified two ways on the live process: index 0 read at
    // item+8 is Package /Script/CoreUObject, and every chunk pointer in the
    // array ends in ...008 while the stride from there is exactly 24.
    std::size_t kFUObjectItemObject;

    // UObject::ProcessEvent, for dispatching UFUNCTIONs - the widget
    // translation and both Kismet traces. Pinned by RVA rather than read
    // from a vtable slot because AActor overrides the slot with an RPC-aware
    // variant, and the base UObject one is the one that must be called here.
    std::uintptr_t kProcessEventRva;

    ReflectionLayout Reflection;
    EngineOffsets    Engine;
};

struct BuildProfile {
    const char*   Name;
    PeFingerprint Fingerprint;
    OffsetTable   Offsets;
};

}  // namespace subliminal_ht
