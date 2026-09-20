// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

#include <array>
#include <cstdint>

// The GetPlayerViewPoint caller gate, which IS the look/aim decoupling: the
// head pose is written back for the render-path caller only, so interaction
// traces, the audio listener, AI perception and replication all keep the clean
// mouse/pad rotation.
//
// Modes are a flat integer so a hotkey can cycle them in game and the log can
// print them:
//
//   kAllCallers (0)          every caller - entangles aim with view, diagnostic
//                            only, and the only mode that counts calls per RVA
//   1 .. kCallerSlots        only callers[mode - 1]
//   kNone (kCallerSlots + 1) no caller - tracking off at the hook
namespace subliminal_ht::inject {

// Matches OffsetTable::kKnownCallerRvas; view_hook.cpp static_asserts the two.
inline constexpr std::size_t kCallerSlots = 16;

using CallerRvas = std::array<std::uintptr_t, kCallerSlots>;

inline constexpr int kAllCallers = 0;
inline constexpr int kFirstCaller = 1;
inline constexpr int kNone = kFirstCaller + static_cast<int>(kCallerSlots);
inline constexpr int kModeCount = kNone + 1;

inline bool IsSingleCaller(int mode) { return mode >= kFirstCaller && mode < kNone; }

// The RVA a single-caller mode is gated on, or 0 for kAllCallers / kNone.
inline std::uintptr_t CallerRva(int mode, const CallerRvas& callers) {
    return IsSingleCaller(mode) ? callers[static_cast<std::size_t>(mode - kFirstCaller)] : 0;
}

inline bool ShouldInject(std::uintptr_t retRva, int mode, const CallerRvas& callers) {
    if (mode == kAllCallers) return true;
    const std::uintptr_t gate = CallerRva(mode, callers);
    return gate != 0 && retRva == gate;
}

// Step the mode by `direction` (+1 / -1), wrapping through the whole range.
inline int Cycle(int mode, int direction) {
    return ((mode + direction) % kModeCount + kModeCount) % kModeCount;
}

}  // namespace subliminal_ht::inject
