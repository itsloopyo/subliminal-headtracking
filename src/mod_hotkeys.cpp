// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "mod_hotkeys.h"

#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "builds/build_registry.h"
#include "inject_mode.h"
#include "logging.h"
#include "view_hook.h"

#include "cameraunlock/input/hotkey_poller.h"
#include "cameraunlock/input/key_binding_registration.h"
#include "cameraunlock/input/key_bindings.h"

namespace subliminal_ht::hotkeys {

namespace {

using cameraunlock::TrackingMode;
using cameraunlock::input::KeyBinding;

// How often the poller samples the keyboard, in milliseconds.
constexpr unsigned kPollIntervalMs = 16;

std::unique_ptr<cameraunlock::input::HotkeyPoller> g_poller;
Session* g_session = nullptr;

// End changes this session only; EnableOnStartup decides the next one.
void ToggleTracking() {
    const bool enabled = !view_hook::TrackingEnabled();
    view_hook::SetTrackingEnabled(enabled);
    Log::Line("hotkey: tracking %s", enabled ? "ON" : "OFF");
}

// The session's mode is an atomic the render thread reads each frame, so the
// cycle applies it here and then saves it.
void CycleTrackingMode() {
    const TrackingMode mode = g_session->CycleMode();
    const char* name = mode == TrackingMode::RotationOnly ? "rotation only"
                     : mode == TrackingMode::PositionOnly ? "position only"
                                                          : "rotation and position";
    Log::Line("hotkey: tracking mode -> %s", name);
    config::SaveTrackingMode(mode);
}

void ToggleYawMode() {
    const bool worldSpaceYaw = !view_hook::WorldSpaceYaw();
    view_hook::SetWorldSpaceYaw(worldSpaceYaw);
    Log::Line("hotkey: yaw mode %s", worldSpaceYaw ? "world" : "local");
    config::SaveWorldSpaceYaw(worldSpaceYaw);
}

void CycleInject(int direction) {
    const int mode = inject::Cycle(view_hook::InjectMode(), direction);
    view_hook::SetInjectMode(mode);
    Log::Line("hotkey: inject mode -> %d (caller RVA 0x%08llx)", mode,
        static_cast<unsigned long long>(
            inject::CallerRva(mode, Offsets().kKnownCallerRvas)));
}

// The table's hotkey codec only lets through a list this parser reads.
std::vector<KeyBinding> Bindings(const char* key, const std::string& list) {
    const cameraunlock::input::KeyBindingsParseResult parsed = cameraunlock::input::ParseKeyBindings(list);
    if (!parsed.ok()) throw std::logic_error(std::string(key) + "='" + list + "': " + parsed.error);
    return parsed.bindings;
}

}  // namespace

void Register(const Config& config, Session& session) {
    g_session = &session;
    g_poller = std::make_unique<cameraunlock::input::HotkeyPoller>();

    // Each list holds every key that fires its action, the Ctrl+Shift chord
    // included. A key without modifiers stays silent while Ctrl and Shift are
    // both held, so Ctrl+Shift+<key> reaches only a binding that names the
    // chord.
    cameraunlock::input::RegisterKeyBindings(*g_poller, Bindings("ToggleKey", config.toggle_key),
                                             [] { ToggleTracking(); });
    cameraunlock::input::RegisterKeyBindings(*g_poller,
                                             Bindings("CycleTrackingModeKey", config.cycle_tracking_mode_key),
                                             [] { CycleTrackingMode(); });
    cameraunlock::input::RegisterKeyBindings(*g_poller, Bindings("YawModeKey", config.yaw_mode_key),
                                             [] { ToggleYawMode(); });
    Log::Line("hotkey: toggle=[%s] cycle tracking mode=[%s] yaw mode=[%s]", config.toggle_key.c_str(),
              config.cycle_tracking_mode_key.c_str(), config.yaw_mode_key.c_str());

    // Re-confirm the render caller in game (step which GPV caller is injected)
    // without a rebuild, after a game patch moves it. Unbound unless the config
    // names keys: stepping off the render caller silently stops the view
    // following the head, which is indistinguishable from a broken mod if it
    // happens by accident.
    cameraunlock::input::RegisterKeyBindings(*g_poller, Bindings("InjectNextKey", config.inject_next_key),
                                             [] { CycleInject(+1); });
    cameraunlock::input::RegisterKeyBindings(*g_poller, Bindings("InjectPreviousKey", config.inject_previous_key),
                                             [] { CycleInject(-1); });
    if (!config.inject_next_key.empty() || !config.inject_previous_key.empty()) {
        Log::Line("dev: inject-mode hotkeys next=[%s] previous=[%s]", config.inject_next_key.c_str(),
                  config.inject_previous_key.c_str());
    }

    g_poller->Start(kPollIntervalMs);
}

void Stop() {
    if (g_poller) g_poller->Stop();
}

}  // namespace subliminal_ht::hotkeys
