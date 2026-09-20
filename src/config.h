// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

#include <string>

#include "cameraunlock/camera/lean_clamp.h"
#include "cameraunlock/effects/head_follow_light.h"
#include "cameraunlock/data/position_settings.h"
#include "cameraunlock/math/smoothing_utils.h"

namespace subliminal_ht {

// The nav-cluster bindings from AGENTS.md, so the same action sits on the same
// key in every mod. Here rather than in mod_hotkeys.cpp because the yaw-mode key
// is user-configurable and its validator has to know which keys the mod has
// already taken - two registrations on one code both fire on a single press.
inline constexpr int kVkToggleTracking = 0x23;   // VK_END
inline constexpr int kVkCycleMode      = 0x21;   // VK_PRIOR (Page Up)

// Virtual-key code the yaw-mode toggle starts on: VK_NEXT (Page Down). Named
// because it is both the struct default below and what a rejected
// [Hotkeys] YawModeKey resolves back to.
inline constexpr int kDefaultYawModeKey = 0x22;

// The smallest collision standoff the parser accepts, in UE units (cm).
//
// It has to EXCEED the engine's 10cm near clip distance, not merely reach it:
// geometry closer to the eye than the near plane is culled, so a wall held at
// exactly the near distance is still not drawn and the player still sees
// through it. Here rather than in config.cpp so the tests assert against the
// number the parser uses instead of a copy of it.
inline constexpr float kNearClipCm = 10.0f;
inline constexpr float kCollisionRadiusMin = kNearClipCm + 1.0f;

struct Config {
    int udp_port = 4242;
    bool enable_on_startup = true;

    // true = yaw turns about the world up-axis, so looking at the floor and
    // turning your head still pans across it. false = yaw turns about the
    // camera's own up-axis, which leans the horizon on a pitched turn.
    // Runtime-toggleable; this is only the value the mod starts in.
    bool world_space_yaw = true;

    float yaw_sensitivity = 1.0f;
    float pitch_sensitivity = 1.0f;
    float roll_sensitivity = 1.0f;
    bool invert_yaw = false;
    bool invert_pitch = false;
    bool invert_roll = false;

    // Smoothing is picked per connection from the packet source address: a
    // tracker on this machine (loopback) uses local_smoothing, a remote network
    // device uses remote_smoothing. Both cover rotation and position.
    float local_smoothing = static_cast<float>(cameraunlock::math::kDefaultLocalSmoothing);
    float remote_smoothing = static_cast<float>(cameraunlock::math::kDefaultRemoteSmoothing);

    bool position_enabled = true;
    float position_sensitivity_x = 1.0f;
    float position_sensitivity_y = 1.0f;
    float position_sensitivity_z = 1.0f;
    float limit_x = cameraunlock::PositionSettings{}.limit_x;
    float limit_y = cameraunlock::PositionSettings{}.limit_y;
    float limit_y_down = cameraunlock::PositionSettings{}.limit_y_down;
    float limit_z = cameraunlock::PositionSettings{}.limit_z;
    float limit_z_back = cameraunlock::PositionSettings{}.limit_z_back;

    // Stop a lean putting the rendered eye inside a wall. On, because the trace
    // has been seen engaging on real geometry in this game: leaning 30cm into a
    // TV at 37cm was cut to 16.7cm, which is the surface less the radius below,
    // and the log line said so on the frame it happened.
    bool collision_enabled = true;
    // How far off a blocking surface the eye is held, in UE units (cm). Must
    // exceed the camera's near clip distance or the wall is culled anyway and
    // the player still sees through it - UE's is 10cm, so this is double it
    // rather than a couple of centimetres clear of it.
    float collision_radius = 20.0f;
    // ETraceTypeQuery index the sweep runs on. Which channel a game's level
    // geometry blocks is a project setting, not an engine constant.
    int collision_channel = 0;
    float collision_release_smoothing =
        cameraunlock::camera::LeanClampSettings{}.release_smoothing;

    // Move the game's own crosshair ring to where the interaction ray actually
    // points. Off leaves it at the centre of the head-tracked picture, which is
    // not where the ray goes once the head is off centre. The interaction
    // prompts are screen furniture anchored to the edges of the frame and are
    // deliberately left where the game puts them.
    bool reticle_follows_aim = true;
    // How far the aim trace reaches, in UE units (cm). Past this the reticle
    // falls back to the aim direction, which is what a target at infinity
    // projects to anyway.
    float aim_trace_distance = 20000.0f;
    // ETraceTypeQuery index the aim trace runs on. Same kind of project
    // setting as collision_channel, and the log prints what the trace found so
    // it can be checked rather than assumed.
    int aim_trace_channel = 0;

    // Point the flashlight down the head-tracked view rather than the mouse aim,
    // and how far it leads the head. The defaults and the bounds are the fleet's,
    // from cameraunlock::effects - 1.5 leads the view, 1.0 matches it, 0 pins the
    // beam back on the aim, which is what the game does unmodded.
    //
    // Read as [Flashlight] Enabled and [Flashlight] Multiplier, which is what
    // resident-evil-requiem ships. Both are deliberately absent from core's alias
    // vocabulary: matching there is section-less, and a bare `Enabled` once
    // resolved onto the master switch and turned whole mods off with a
    // [Flashlight] section present. Reading them by their own section is what
    // keeps that from happening here.
    cameraunlock::effects::HeadFollowLightSettings flashlight;

    // Ctrl+Shift+U / Ctrl+Shift+J cycle which GetPlayerViewPoint caller gets
    // the head pose. Only useful for re-confirming the render caller after a
    // game patch, so off unless asked for.
    bool inject_hotkeys = false;

    // Dev only: periodically list the live UMG objects whose name or class
    // looks like a reticle or interaction prompt, so the widgets to move can be
    // identified. Their names live in cooked Blueprint assets, so they cannot
    // be read out of the EXE.
    bool widget_dump = false;

    // Virtual-key code for the yaw-mode toggle. Ctrl+Shift+H does the same job
    // and is not configurable.
    int yaw_mode_key = kDefaultYawModeKey;
};

void config_load(const std::string& exe_dir, Config& out);
void config_write_default_if_missing(const std::string& exe_dir);

}  // namespace subliminal_ht
