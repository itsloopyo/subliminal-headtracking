// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "build_profile.h"


namespace subliminal_ht::builds {

extern const BuildProfile kSteamProfile_20260906;

const BuildProfile kSteamProfile_20260906 = {
    /* Name        */ "steam-win64-20260906",
    /* Fingerprint */ { 0x841F96CEu, 0x0B0F3000u, 0x0AD39823u },
    /* Offsets     */ {
        /* kGetPlayerViewPointRva */ 0x04069d80ULL,

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

        /* kFUObjectItemObject */ 0x08,

        /* kProcessEventRva */ 0x01568d40ULL,

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

        /* Engine */ {
            /* kShowMouseCursorOffset        */ 0x554,
            /* kShowMouseCursorMask          */ 0x1u,
            /* kPawnOffset                   */ 0x2f0,
            /* kWorldNetDriverOffset         */ 0x038,
        },
    },
};

}  // namespace subliminal_ht::builds
