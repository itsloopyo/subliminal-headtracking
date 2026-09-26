// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

#include <string>

#include "cameraunlock/camera/lean_clamp.h"
#include "cameraunlock/config/config_concepts.g.h"
#include "cameraunlock/config/config_owner.h"
#include "cameraunlock/config/defaults_file.h"
#include "cameraunlock/config/legacy_import.h"
#include "cameraunlock/data/position_settings.h"
#include "cameraunlock/effects/head_follow_light.h"
#include "cameraunlock/math/smoothing_utils.h"
#include "cameraunlock/tracking/tracking_mode.h"

namespace subliminal_ht {

// The settings CameraUnlock.ini holds, at their defaults.
struct Config {
    int udp_port = 4242;
    bool enable_on_startup = true;

    // true = yaw turns about the world up-axis, so looking at the floor and
    // turning your head still pans across it. false = yaw turns about the
    // camera's own up-axis, which leans the horizon on a pitched turn.
    bool world_space_yaw = true;

    // The tracking mode at startup, the pair the mode hotkey saves.
    bool rotation_enabled = true;
    bool position_enabled = true;

    // Smoothing is picked per connection from the packet source address: a
    // tracker on this machine (loopback) uses local_smoothing, a remote network
    // device uses remote_smoothing. Both cover rotation and position.
    float local_smoothing = static_cast<float>(cameraunlock::math::kDefaultLocalSmoothing);
    float remote_smoothing = static_cast<float>(cameraunlock::math::kDefaultRemoteSmoothing);

    float position_limit_x = cameraunlock::PositionSettings{}.limit_x;
    float position_limit_y = cameraunlock::PositionSettings{}.limit_y;
    float position_limit_y_down = cameraunlock::PositionSettings{}.limit_y_down;
    float position_limit_z = cameraunlock::PositionSettings{}.limit_z;
    float position_limit_z_back = cameraunlock::PositionSettings{}.limit_z_back;

    // Stop a lean putting the rendered eye inside a wall. The trace has been
    // seen engaging on real geometry in this game: leaning 30cm into a TV was
    // cut short, and the log line said so on the frame it happened.
    bool collision_enabled = true;
    // How far off a blocking surface the eye is held, in UE units (cm). Must
    // exceed the camera's near clip distance or the wall is culled anyway and
    // the player still sees through it - UE's is 10cm, so this is double it
    // rather than a couple of centimetres clear of it.
    float collision_margin = 20.0f;
    // ETraceTypeQuery index the sweep runs on. Which channel a game's level
    // geometry blocks is a project setting, not an engine constant.
    int collision_channel = 0;
    float collision_release_smoothing =
        cameraunlock::camera::LeanClampSettings{}.release_smoothing;

    std::string toggle_key =
        cameraunlock::config::schema::ConceptTraits<cameraunlock::config::schema::Concept::ToggleKey>::kCanonicalDefault;
    std::string cycle_tracking_mode_key =
        cameraunlock::config::schema::ConceptTraits<cameraunlock::config::schema::Concept::CycleTrackingModeKey>::kCanonicalDefault;
    std::string yaw_mode_key =
        cameraunlock::config::schema::ConceptTraits<cameraunlock::config::schema::Concept::YawModeKey>::kCanonicalDefault;

    // Point the flashlight down the head-tracked view rather than the mouse aim,
    // and how far it leads the head. The defaults and the bounds are the fleet's,
    // from cameraunlock::effects - 1.5 leads the view, 1.0 matches it, 0 pins the
    // beam back on the aim, which is what the game does unmodded.
    bool light_follows_head = true;
    float light_multiplier = cameraunlock::effects::kDefaultLightMultiplier;

    // How far the aim trace reaches, in UE units (cm). Past this the reticle
    // falls back to the aim direction, which is what a target at infinity
    // projects to anyway.
    float aim_trace_distance = 20000.0f;
    // ETraceTypeQuery index the aim trace runs on. Same kind of project setting
    // as collision_channel, and the log prints what the trace found so it can be
    // checked rather than assumed.
    int aim_trace_channel = 0;

    // Dev only: keys that step which GetPlayerViewPoint caller gets the head
    // pose, forward and back. Only useful for re-confirming the render caller
    // after a game patch, so unbound unless asked for.
    std::string inject_next_key;
    std::string inject_previous_key;

    // Dev only: periodically list the live UMG objects whose name or class
    // looks like a reticle or interaction prompt, so the widgets to move can be
    // identified. Their names live in cooked Blueprint assets, so they cannot
    // be read out of the EXE.
    bool widget_dump = false;
};

}  // namespace subliminal_ht

// CameraUnlock.ini, beside the game exe, in cameraunlock-core's canonical config
// format. One ConfigOwner reads and writes it; nothing else in the mod touches
// it. HeadTracking.ini, the file every earlier build read, is imported once
// while CameraUnlock.ini is absent and is never written.
namespace subliminal_ht::config {

cameraunlock::config::ConfigTable<Config> Table();

cameraunlock::config::RenderHeader Header();

// HeadTracking.ini through the frozen reader in src/legacy_config/, mapped into
// Config.
cameraunlock::config::LegacyImport<Config> Import();

// The owner's options for CameraUnlock.ini in `exe_dir`, a full path, with
// HeadTracking.ini beside it as the legacy file and Defaults.ini where
// `defaults` says.
cameraunlock::config::ConfigOwnerOptions<Config> OwnerOptions(const std::wstring& exe_dir,
                                                              cameraunlock::config::DefaultsFile defaults);

// Reads, imports or creates CameraUnlock.ini in `exe_dir`, logs what the owner
// reports, and returns the settings the session runs on. Call once, from the
// bootstrap thread, with the log open. `defaults` is DefaultsFile::PerUser() in
// the mod.
Config Load(const std::wstring& exe_dir, cameraunlock::config::DefaultsFile defaults);

// The tracking mode the settings start in. The table never gives both rows
// false.
cameraunlock::TrackingMode StartupTrackingMode(const Config& config);

// Saves the value a hotkey has just applied. The session keeps it whether or
// not the save succeeds; a failed save is logged. Called on the hotkey thread.
void SaveWorldSpaceYaw(bool world_space_yaw);
void SaveTrackingMode(cameraunlock::TrackingMode mode);

}  // namespace subliminal_ht::config
