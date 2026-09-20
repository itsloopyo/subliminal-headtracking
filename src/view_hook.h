// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

#include <cstdint>

#include "config.h"
#include "session.h"

// The APlayerController::GetPlayerViewPoint hook: the one place the head pose
// reaches the game. Everything it needs from the rest of the mod arrives once,
// at Install(), so the hot path owns no state that the bootstrap also writes.
namespace subliminal_ht::view_hook {

// All three must outlive the hook, which the bootstrap satisfies by owning them
// for the life of the process - the hook is never uninstalled while the game is
// running, so there is no window in which one could be freed under it.
struct Dependencies {
    const Config* config;
    Session*      session;
    cameraunlock::UdpReceiver* receiver;
};

// Create and enable the trampoline against the active build profile's RVA.
// Returns false, having logged why, if MinHook refuses; the caller then leaves
// the game vanilla.
bool Install(const Dependencies& deps);

bool TrackingEnabled();
void SetTrackingEnabled(bool enabled);

// true = world-space yaw (horizon-locked, FRotator addition); false = camera-
// local yaw (quaternion post-multiply, leans on pitched turns).
bool WorldSpaceYaw();
void SetWorldSpaceYaw(bool worldSpaceYaw);

// Which GetPlayerViewPoint caller gets the head pose. See inject_mode.h.
int  InjectMode();
void SetInjectMode(int mode);

}  // namespace subliminal_ht::view_hook
