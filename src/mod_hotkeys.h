// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

#include "config.h"
#include "session.h"

// The mod's key bindings, from the key lists in CameraUnlock.ini: the three
// actions, and the dev pair that re-confirms the render caller in game. Every
// binding does its work through view_hook or the session and says what it did
// in the log, and the mode cycle and the yaw toggle save what they applied.
namespace subliminal_ht::hotkeys {

// Register the bindings and start polling. `session` must outlive the poller.
void Register(const Config& config, Session& session);

// Stop the polling thread. Safe to call when nothing was registered.
void Stop();

}  // namespace subliminal_ht::hotkeys
