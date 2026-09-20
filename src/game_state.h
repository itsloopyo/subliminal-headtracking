// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

#include <cstdint>

// When head tracking is allowed to touch the view at all.
//
// Two independent gates, both polled from the live player controller on the
// game thread, never latched:
//
//   gameplay - the player has control of the character, rather than a menu, a
//              loading screen or a pause having it.
//   offline  - the world has no net driver.
//
// The offline gate exists because decoupling look from aim would be an
// advantage over other people, so it fails closed: if the net state cannot be
// read, the answer is "networked" and tracking stays off. An unreadable frame
// never enables tracking.
//
// Subliminal ships no multiplayer, so in the shipping game this gate is always
// open. It is here because reading UWorld::NetDriver costs two pointer loads,
// which is cheaper than the claim being an assumption.
namespace subliminal_ht::game_state {

struct Verdict {
    bool InGameplay = false;
    bool Offline    = false;
    // False until the net-mode probe has actually returned an answer. Kept
    // separate from Offline so the log can tell "we know somebody else is here"
    // from "we could not ask", which are the same decision but not the same
    // bug.
    bool NetStateKnown = false;
};

// Re-evaluate for this frame. `controller` is the APlayerController the
// GetPlayerViewPoint hook was called on. Three guarded loads in total, so it
// runs on every call rather than on a timer.
Verdict Evaluate(std::uintptr_t controller);

// Log a transition in either gate, so a session log shows exactly when and why
// tracking stood down without the heartbeat having to land inside the window.
void LogTransitions(const Verdict& v);

// The pawn this controller possesses, or 0. AController::Pawn, resolved once
// through the engine's reflection data. The aim trace and the lean sweep both
// need it: it is what they exclude, so a ray that starts inside the player's
// own capsule does not stop there.
std::uintptr_t PossessedPawn(std::uintptr_t controller);

}  // namespace subliminal_ht::game_state
