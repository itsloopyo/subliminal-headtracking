// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once
#include <windows.h>
#include "build_profile.h"

namespace subliminal_ht
{
    namespace builds
    {
        enum class MatchResult
        {
            DiscoveryFailed,
            Matched,      // Active profile set; mod can run.
            ReadFailed,   // Could not read the PE header.
        };

        MatchResult SelectProfile(HMODULE host);
        const BuildProfile& ActiveProfile();
        bool UsesRuntimeDiscovery();
        bool AcceptEngineOffsets(std::size_t pawn, std::size_t netDriver);
        std::uint32_t RuntimeViewSlot();
    }

    // Accessor for the active profile's offset table. Must run after
    // SelectProfile() returns Matched.
    inline const OffsetTable& Offsets()
    {
        return builds::ActiveProfile().Offsets;
    }
}
