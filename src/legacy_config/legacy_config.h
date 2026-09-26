// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

#include <string>
#include <vector>

// The pre-canonical HeadTracking.ini reader, frozen. It reads a file the way
// the last build before the canonical config format did, so a player's old
// file is carried over as that build read it. Never edit anything in this
// folder: CMakeLists.txt pins every file here by hash.
//
// Frozen from src/config.cpp and src/config.h at bf04d3e (config_load and the
// helpers it calls), with three changes: it fills this frozen copy of that
// commit's settings and their defaults instead of the mod's Config, it writes
// nothing (config_write_default_if_missing stays with the mod), and it lives
// in namespace subliminal_ht::legacy. The defaults and bounds are written as
// the literals the code held then (cameraunlock-core's PositionSettings
// limits, its smoothing defaults, LeanClampSettings::release_smoothing,
// kDefaultLightMultiplier / kMaxLightMultiplier, kMaxSensitivity and
// kMaxPositionLimit), so a later change to core cannot move what an old file
// means. The value guards it calls (ReadFloatChecked, ReadRawValue,
// IsBindableVirtualKey) and IniReader are core's, which core keeps frozen.
namespace subliminal_ht::legacy {

struct Config {
    // [Network]
    int udp_port = 4242;

    // [General]
    bool enable_on_startup = true;
    bool world_space_yaw = true;

    // [Rotation]
    float yaw_sensitivity = 1.0f;
    float pitch_sensitivity = 1.0f;
    float roll_sensitivity = 1.0f;
    bool invert_yaw = false;
    bool invert_pitch = false;
    bool invert_roll = false;
    float local_smoothing = 0.0f;
    float remote_smoothing = 0.15f;

    // [Position]. A file without a usable LimitYDown takes LimitY for it.
    bool position_enabled = true;
    float position_sensitivity_x = 1.0f;
    float position_sensitivity_y = 1.0f;
    float position_sensitivity_z = 1.0f;
    float limit_x = 0.30f;
    float limit_y = 0.20f;
    float limit_y_down = 0.20f;
    float limit_z = 0.40f;
    float limit_z_back = 0.10f;

    // [Collision]
    bool collision_enabled = true;
    float collision_radius = 20.0f;
    int collision_channel = 0;
    float collision_release_smoothing = 0.9f;

    // [Reticle]
    bool reticle_follows_aim = true;
    float aim_trace_distance = 20000.0f;
    int aim_trace_channel = 0;

    // [Flashlight]
    bool flashlight_follows_head = true;
    float flashlight_multiplier = 1.5f;

    // [Dev]
    bool inject_hotkeys = false;
    bool widget_dump = false;

    // [Hotkeys]. A virtual-key code, read with IniReader::ReadHex; one that is
    // not bindable, or is End or Page Up, is replaced by 0x22.
    int yaw_mode_key = 0x22;  // VK_NEXT (Page Down)
};

// Reads `ini_path` into `out`; keys the file lacks keep their defaults. Returns
// whether the file was there to read (IniReader::Open).
bool Load(const std::string& ini_path, Config& out);

struct Key {
    const char* section;
    const char* key;
};

// Every key Load takes a value from.
std::vector<Key> ReadKeys();

}  // namespace subliminal_ht::legacy
