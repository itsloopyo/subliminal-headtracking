// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

// v0.1.0's reader and startup code (tag v0.1.0, commit 68b0863). No dev
// pre-release was published.
//
// src/config.cpp, src/config.h and src/logging.h beside this file are byte
// copies of v0.1.0's, compiled here as they shipped, with their namespace
// renamed by the macro below so they can sit in one program beside this
// build's subliminal_ht. The cameraunlock-core sources they compile hold the
// same code at v0.1.0's pin (665a345) and at this repo's (CMakeLists.txt pins
// them by hash; provenance.tsv names the one comment that differs). What is
// transcribed is the startup code that consumed the settings, which cannot be
// compiled into a test because it hooks the game:
//
//   src/headtracking_mod.cpp  lines 42-74    ApplyConfigToSession
//                             lines 107-115  LoadConfig
//   src/view_hook.cpp         lines 600-616  Install: the startup enable, the
//                                            yaw mode, the lean clamp and the
//                                            two trace channels
//                             lines 413, 457-471, 503, 582-585  the per-frame
//                                            reads of collision_enabled,
//                                            reticle_follows_aim,
//                                            aim_trace_distance, widget_dump
//                                            and flashlight
//   src/mod_hotkeys.cpp       lines 77-93    the hotkey registrations, as data

#define subliminal_ht subliminal_ht_v010
#include "src/config.cpp"
#undef subliminal_ht

#include "oracle_reader.h"

namespace subliminal_oracle {

namespace {

constexpr int kVkY = 0x59;
constexpr int kVkG = 0x47;
constexpr int kVkH = 0x48;
constexpr int kVkU = 0x55;
constexpr int kVkJ = 0x4A;

constexpr unsigned kNav = 0;
constexpr unsigned kChord = 3;

}  // namespace

Published Read(const std::string& exe_dir) {
    subliminal_ht_v010::Config g_config;
    subliminal_ht_v010::config_write_default_if_missing(exe_dir);
    subliminal_ht_v010::config_load(exe_dir, g_config);

    Published p;
    p.udp_port = g_config.udp_port;
    p.tracking_enabled = g_config.enable_on_startup;
    p.world_space_yaw = g_config.world_space_yaw;

    p.yaw_sens = g_config.yaw_sensitivity;
    p.pitch_sens = g_config.pitch_sensitivity;
    p.roll_sens = g_config.roll_sensitivity;
    p.invert_yaw = g_config.invert_yaw;
    p.invert_pitch = g_config.invert_pitch;
    p.invert_roll = g_config.invert_roll;

    p.pos_sens_x = g_config.position_sensitivity_x;
    p.pos_sens_y = g_config.position_sensitivity_y;
    p.pos_sens_z = g_config.position_sensitivity_z;
    p.limit_x = g_config.limit_x;
    p.limit_y = g_config.limit_y;
    p.limit_y_down = g_config.limit_y_down;
    p.limit_z = g_config.limit_z;
    p.limit_z_back = g_config.limit_z_back;

    p.local_smoothing = g_config.local_smoothing;
    p.remote_smoothing = g_config.remote_smoothing;

    p.tracking_mode = g_config.position_enabled ? 0 : 1;

    p.collision_enabled = g_config.collision_enabled;
    p.collision_radius = g_config.collision_radius;
    p.collision_channel = g_config.collision_channel;
    p.collision_release_smoothing = g_config.collision_release_smoothing;
    p.reticle_follows_aim = g_config.reticle_follows_aim;
    p.aim_trace_distance = g_config.aim_trace_distance;
    p.aim_trace_channel = g_config.aim_trace_channel;
    p.light_follows_head = g_config.flashlight.follows_head;
    p.light_multiplier = g_config.flashlight.multiplier;
    p.widget_dump = g_config.widget_dump;

    p.hotkeys.push_back({kToggle, subliminal_ht_v010::kVkToggleTracking, kNav});
    p.hotkeys.push_back({kCycleMode, subliminal_ht_v010::kVkCycleMode, kNav});
    p.hotkeys.push_back({kYawMode, g_config.yaw_mode_key, kNav});
    p.hotkeys.push_back({kToggle, kVkY, kChord});
    p.hotkeys.push_back({kCycleMode, kVkG, kChord});
    p.hotkeys.push_back({kYawMode, kVkH, kChord});
    if (g_config.inject_hotkeys) {
        p.hotkeys.push_back({kInjectNext, kVkU, kChord});
        p.hotkeys.push_back({kInjectPrevious, kVkJ, kChord});
    }
    return p;
}

}  // namespace subliminal_oracle
