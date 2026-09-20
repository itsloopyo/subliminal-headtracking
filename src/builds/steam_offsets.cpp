// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "build_profile.h"

// Steam Win64 build of Subliminal (Subliminal-Win64-Shipping.exe, UE 5.7.4),
// the Subliminal/Binaries/Win64 shipping exe.
//
// To add support for a new Steam build: do NOT edit kSteamProfile_<date> in
// place. Append a new `extern const BuildProfile kSteamProfile_YYYYMMDD = {...}`
// below, register it at the top of kKnownProfiles in build_registry.cpp, and
// keep older profiles forever (the PE fingerprint routes each user to theirs).

namespace subliminal_ht::builds {

extern const BuildProfile kSteamProfile_20260906;

// ---- Steam Win64 build (PE TimeDateStamp 0x841F96CE) ----
const BuildProfile kSteamProfile_20260906 = {
    /* Name        */ "steam-win64-20260906",
    /* Fingerprint */ { 0x841F96CEu, 0x0B0F3000u, 0x0AD39823u },
    /* Offsets     */ {
        // The checkf strings that name GetPlayerViewPoint's out-parameters
        // are stripped from this shipping build, so the anchor is the
        // reflection data instead. AController::GetPlayerViewPoint is a
        // BlueprintCallable UFUNCTION, so the EXE carries its name as an
        // ASCII string (RVA 0x08738ed8) and, in the native-function
        // registration table at 0x08750370, the address of its generated
        // exec thunk (RVA 0x03b56480). That thunk pops two out-parameter
        // references off the script frame and ends in a virtual call on the
        // invoking object, through vtable displacement 0x840, so GPV is
        // vtable slot 264. APlayerController's vtable is at RVA
        // 0x08941728, stored by its constructor (0x0404c470), which
        // GetPrivateStaticClass (0x04042e90) reaches through
        // InternalConstructor<APlayerController> (0x0404abe0) - and that
        // getter is the one that passes the wide string L"PlayerController"
        // and the class size 0x740 to GetPrivateStaticClassBody, which is
        // what says it is APlayerController's and not some other class's.
        //
        // vtable + 0x840 is the RVA below. Its body confirms it: an FName
        // lookup of EName 0x142, an IsInState call, a state byte test at
        // controller+0x168, the cached-POV copy out of
        // LastSpectatorSyncLocation/Rotation (+0x3e0 / +0x3f8), and
        // otherwise PlayerCameraManager at controller+0x368 with its
        // CameraCachePrivate timestamp at +0x1550.
        /* kGetPlayerViewPointRva */ 0x04069d80ULL,

        // ULocalPlayer::GetViewPoint (fn RVA 0x03df8014) is the
        // FMinimalViewInfo builder the renderer's projection comes from,
        // identified by its shape rather than by a symbol: it copies the
        // camera cache view field by field into the out-parameter struct,
        // then takes PlayerCameraManager from controller+0x368, stores that
        // manager's GetFOVAngle (vtable displacement 0x800) into the struct
        // at +0x30, and calls GetPlayerViewPoint (vtable displacement 0x840)
        // with the struct's base as the location out-parameter and base+0x18
        // as the rotation one. The return address of that call, RVA
        // 0x03df8272, is therefore the render path, and the two
        // FMinimalViewInfo offsets below are the +0x30 and +0x18 it uses.
        // Confirmed in game: injecting for this caller alone moves the
        // rendered view, and the interaction trace still follows the mouse.
        /* kKnownCallerRvas */ {{
            0x03df8272ULL,  // 1: fn 0x03df8014 - RENDER PATH
            0x0ULL, 0x0ULL, 0x0ULL, 0x0ULL, 0x0ULL, 0x0ULL, 0x0ULL,
            0x0ULL, 0x0ULL, 0x0ULL, 0x0ULL, 0x0ULL, 0x0ULL, 0x0ULL, 0x0ULL,
        }},
        /* kDefaultInjectMode     */ 1,

        /* MinimalViewInfoLayout */ {
            /* kFovOffset      */ 0x30,
            /* kRotationStride */ 0x18,
        },

        // GUObjectArray.ObjObjects and FNamePool, located on the running
        // process and then pinned: ObjObjects is the only .data pointer
        // whose chunk array reaches an FUObjectItem holding the live player
        // controller, and FNamePool is the only .data block array whose
        // Blocks[] hold the allocations the engine's packed name strings
        // live in. MaxElements 2,162,688 = 33 chunks of 0x10000, which is
        // where the chunk size below comes from; NumElements sits at +0x14
        // between MaxElements and MaxChunks.
        //
        // The UObject struct offsets are engine-version-bound rather than
        // build-bound.
        /* UObjectGlobals */ {
            /* kObjObjects       */ 0x0a511420ULL,
            /* kObjObjects_Num   */ 0x14,
            /* kFUObjectItemSize */ 0x18,
            /* kChunkNumElems    */ 0x10000,
            /* kFNamePool        */ 0x0a443000ULL,
            /* kFNamePoolBlocks  */ 0x10,
            /* kClassPrivate     */ 0x10,
            /* kNamePrivate      */ 0x18,
            /* kOuterPrivate     */ 0x20,
        },

        // See build_profile.h: UE 5.7 puts UObjectBase* eight bytes into the
        // item rather than at its head.
        /* kFUObjectItemObject */ 0x08,

        // UObject::ProcessEvent, identified by its body: the unreachable-flag
        // bit test on ObjectFlags, the FUNC_Native test against 0x400 on the
        // function's flags word at +0xb0, the ParmsSize read as a 16-bit field
        // at +0xb6, and the aligned alloca of Function->PropertiesSize (+0x58)
        // that builds the parameter frame.
        /* kProcessEventRva */ 0x01568d40ULL,

        // ReflectionLayout, read off APlayerController's own property list
        // against a class whose field offsets are already known: walking
        // ChildProperties at +0x50 with Next at +0x18, NamePrivate at +0x20 and
        // Offset_Internal at +0x44 reproduces Player=0x350,
        // AcknowledgedPawn=0x358, MyHUD=0x360 and
        // PlayerCameraManager=0x368 in declaration order - and 0x368 is the
        // displacement GetPlayerViewPoint itself dereferences, which is the
        // independent check that the layout is right rather than merely
        // self-consistent.
        /* Reflection */ {
            /* kFField_ClassPrivate     */ 0x08,
            /* kFField_Next             */ 0x18,
            /* kFField_NamePrivate      */ 0x20,
            /* kFProperty_ArrayDim      */ 0x30,
            /* kFProperty_ElementSize   */ 0x34,
            /* kFProperty_Offset        */ 0x44,
            /* kFFieldClass_Name        */ 0x00,
            /* kUStruct_SuperStruct     */ 0x40,
            /* kUStruct_ChildProperties */ 0x50,
            /* kUStruct_PropertiesSize  */ 0x58,
        },

        // Every one of these came out of the engine's own property tables.
        // bShowMouseCursor is the first of a run of packed bools -
        // bEnableClickEvents, bEnableTouchEvents, bEnableMouseOverEvents
        // follow it in the same dword, in APlayerController's declaration
        // order, which is the cross-check that 0x554 is the bitfield and
        // not a coincidence.
        /* Engine */ {
            /* kShowMouseCursorOffset        */ 0x554,
            /* kShowMouseCursorMask          */ 0x1u,
            /* kPawnOffset                   */ 0x2f0,
            /* kWorldNetDriverOffset         */ 0x038,
        },
    },
};

}  // namespace subliminal_ht::builds
