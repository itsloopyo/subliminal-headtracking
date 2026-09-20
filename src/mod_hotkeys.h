// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

#include "config.h"
#include "session.h"

// The mod's key bindings: the AGENTS.md nav-cluster defaults, their Ctrl+Shift
// chord alternatives, and the opt-in dev pair that re-confirms the render
// caller in game. Every binding does its work through view_hook or the session
// and says what it did in the log, so this is the only place that knows which
// key means what.
namespace subliminal_ht::hotkeys {

// Register the bindings and start polling. `session` must outlive the poller.
void Register(const Config& config, Session& session);

// Stop the polling thread. Safe to call when nothing was registered.
void Stop();

}  // namespace subliminal_ht::hotkeys
