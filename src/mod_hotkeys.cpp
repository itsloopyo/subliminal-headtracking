// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "mod_hotkeys.h"

#include <memory>

#include <windows.h>

#include "builds/build_registry.h"
#include "inject_mode.h"
#include "logging.h"
#include "view_hook.h"

#include "cameraunlock/input/chord_hotkeys.h"
#include "cameraunlock/input/hotkey_poller.h"

namespace subliminal_ht::hotkeys {

namespace {

using cameraunlock::TrackingMode;
using cameraunlock::input::ChordGuarded;
using cameraunlock::input::NavGuarded;

// The Ctrl+Shift chord cluster (T/Y/U/G/H/J) from AGENTS.md, so the same action
// sits on the same chord in every mod. The nav-cluster codes are in config.h,
// where the yaw-mode key's validator can see them.
constexpr int kVkY       = 0x59;
constexpr int kVkG       = 0x47;
constexpr int kVkH       = 0x48;
constexpr int kVkU       = 0x55;
constexpr int kVkJ       = 0x4A;

// How often the poller samples the keyboard, in milliseconds.
constexpr unsigned kPollIntervalMs = 16;

std::unique_ptr<cameraunlock::input::HotkeyPoller> g_poller;
Session* g_session = nullptr;

void ToggleTracking() {
    const bool enabled = !view_hook::TrackingEnabled();
    view_hook::SetTrackingEnabled(enabled);
    Log::Line("hotkey: tracking %s", enabled ? "ON" : "OFF");
}

void CycleTrackingMode() {
    const TrackingMode mode = g_session->CycleMode();
    const char* name = mode == TrackingMode::RotationOnly ? "rotation only"
                     : mode == TrackingMode::PositionOnly ? "position only"
                                                          : "rotation and position";
    Log::Line("hotkey: tracking mode -> %s", name);
}

void ToggleYawMode() {
    const bool worldSpaceYaw = !view_hook::WorldSpaceYaw();
    view_hook::SetWorldSpaceYaw(worldSpaceYaw);
    Log::Line("hotkey: yaw mode %s", worldSpaceYaw ? "world" : "local");
}

void CycleInject(int direction) {
    const int mode = inject::Cycle(view_hook::InjectMode(), direction);
    view_hook::SetInjectMode(mode);
    Log::Line("hotkey: inject mode -> %d (caller RVA 0x%08llx)", mode,
        static_cast<unsigned long long>(
            inject::CallerRva(mode, Offsets().kKnownCallerRvas)));
}

}  // namespace

void Register(const Config& config, Session& session) {
    g_session = &session;
    g_poller = std::make_unique<cameraunlock::input::HotkeyPoller>();

    // Nav-cluster defaults. Suppressed when Ctrl+Shift is held so the chord
    // path is the sole trigger.
    g_poller->AddHotkey(kVkToggleTracking, NavGuarded([] { ToggleTracking(); }));
    g_poller->AddHotkey(kVkCycleMode,      NavGuarded([] { CycleTrackingMode(); }));
    g_poller->AddHotkey(config.yaw_mode_key, NavGuarded([] { ToggleYawMode(); }));

    // Ctrl+Shift chord alternatives (Y/G/H cluster).
    g_poller->AddHotkey(kVkY, ChordGuarded([] { ToggleTracking(); }));
    g_poller->AddHotkey(kVkG, ChordGuarded([] { CycleTrackingMode(); }));
    g_poller->AddHotkey(kVkH, ChordGuarded([] { ToggleYawMode(); }));

    // Re-confirm the render caller in-game (cycle which GPV caller is injected)
    // without a rebuild, after a game patch moves it. Opt-in: cycling off the
    // render caller silently stops the view following the head, which is
    // indistinguishable from a broken mod if it happens by accident.
    if (config.inject_hotkeys) {
        g_poller->AddHotkey(kVkU, ChordGuarded([] { CycleInject(+1); }));
        g_poller->AddHotkey(kVkJ, ChordGuarded([] { CycleInject(-1); }));
        Log::Line("dev: inject-mode hotkeys enabled (Ctrl+Shift+U next / Ctrl+Shift+J prev)");
    }

    g_poller->Start(kPollIntervalMs);
}

void Stop() {
    if (g_poller) g_poller->Stop();
}

}  // namespace subliminal_ht::hotkeys
