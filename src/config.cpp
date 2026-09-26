// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "config.h"

#include <cstdio>
#include <string>
#include <windows.h>

#include "legacy_config/legacy_config.h"
#include "logging.h"

namespace subliminal_ht {

namespace {

constexpr const char* kIniName = "HeadTracking.ini";

std::string ini_path(const std::string& exe_dir) {
    return exe_dir + "\\" + kIniName;
}

}  // namespace

void config_load(const std::string& exe_dir, Config& out) {
    legacy::Config read;
    legacy::Load(ini_path(exe_dir), read);

    out.udp_port = read.udp_port;
    out.enable_on_startup = read.enable_on_startup;
    out.world_space_yaw = read.world_space_yaw;
    out.yaw_sensitivity = read.yaw_sensitivity;
    out.pitch_sensitivity = read.pitch_sensitivity;
    out.roll_sensitivity = read.roll_sensitivity;
    out.invert_yaw = read.invert_yaw;
    out.invert_pitch = read.invert_pitch;
    out.invert_roll = read.invert_roll;
    out.local_smoothing = read.local_smoothing;
    out.remote_smoothing = read.remote_smoothing;
    out.position_enabled = read.position_enabled;
    out.position_sensitivity_x = read.position_sensitivity_x;
    out.position_sensitivity_y = read.position_sensitivity_y;
    out.position_sensitivity_z = read.position_sensitivity_z;
    out.limit_x = read.limit_x;
    out.limit_y = read.limit_y;
    out.limit_y_down = read.limit_y_down;
    out.limit_z = read.limit_z;
    out.limit_z_back = read.limit_z_back;
    out.collision_enabled = read.collision_enabled;
    out.collision_radius = read.collision_radius;
    out.collision_channel = read.collision_channel;
    out.collision_release_smoothing = read.collision_release_smoothing;
    out.reticle_follows_aim = read.reticle_follows_aim;
    out.aim_trace_distance = read.aim_trace_distance;
    out.aim_trace_channel = read.aim_trace_channel;
    out.flashlight.follows_head = read.flashlight_follows_head;
    out.flashlight.multiplier = read.flashlight_multiplier;
    out.yaw_mode_key = read.yaw_mode_key;
    out.inject_hotkeys = read.inject_hotkeys;
    out.widget_dump = read.widget_dump;
}

void config_write_default_if_missing(const std::string& exe_dir) {
    const std::string p = ini_path(exe_dir);
    if (GetFileAttributesA(p.c_str()) != INVALID_FILE_ATTRIBUTES) return;

    FILE* f = nullptr;
    const errno_t err = fopen_s(&f, p.c_str(), "w");
    if (!f) {
        // Not fatal - the shipped defaults are the same numbers this file would
        // have carried - but it has to be said, because the symptom otherwise is
        // a mod with no settings file next to it and no reason given.
        Log::Line("config: could not create %s (errno %d). The built-in defaults are in "
                  "use, and settings cannot be changed until this path is writable.",
                  p.c_str(), static_cast<int>(err));
        return;
    }
    std::fprintf(f,
        "; Subliminal Head Tracking - configuration\n"
        "; Edit values, restart the game to apply.\n\n"
        "[Network]\n"
        "UdpPort=%d\n\n"
        "[General]\n"
        "EnableOnStartup=1\n"
        "; Yaw mode: 1 = horizon-locked yaw (default), 0 = camera-local yaw.\n"
        "; Page Down (or Ctrl+Shift+H) toggles it in game.\n"
        "WorldSpaceYaw=1\n\n"
        "[Rotation]\n"
        "YawSensitivity=1.0\n"
        "PitchSensitivity=1.0\n"
        "RollSensitivity=1.0\n"
        "InvertYaw=0\n"
        "InvertPitch=0\n"
        "InvertRoll=0\n"
        "; Smoothing 0.0 (responsive) - 1.0 (heavy). Covers rotation and position.\n"
        "; The value is picked per connection from the packet source address:\n"
        "; LocalSmoothing for a tracker sending from this PC over loopback\n"
        "; (127.0.0.1), RemoteSmoothing for anything else - including a tracker on\n"
        "; this same PC that sends to the machine's LAN address instead.\n"
        "LocalSmoothing=0.0\n"
        "RemoteSmoothing=0.15\n\n"
        "[Position]\n"
        "Enabled=1\n"
        "SensitivityX=1.0\n"
        "SensitivityY=1.0\n"
        "SensitivityZ=1.0\n"
        "LimitX=0.30\n"
        "LimitY=0.20\n"
        "LimitYDown=%.2f\n"
        "LimitZ=0.40\n"
        "LimitZBack=0.10\n\n"
        "[Collision]\n"
        "; Stop a lean putting your eye inside a wall. The mod sweeps the game's\n"
        "; own collision from where the camera really is towards where your head\n"
        "; asks it to go, and cuts the lean to whatever the room leaves.\n"
        "CollisionEnabled=%d\n"
        "; How far off a surface the eye is held, in centimetres.\n"
        "CollisionRadius=%.0f\n"
        "; Which trace channel level geometry blocks (ETraceTypeQuery index).\n"
        "CollisionChannel=%d\n"
        "; How quickly the lean opens back up once the wall clears. 0.9 is about\n"
        "; a fifth of a second; tightening is always instant.\n"
        "CollisionReleaseSmoothing=%.2f\n\n"
        "[Reticle]\n"
        "; Move the game's crosshair ring to where you are actually pointing.\n"
        "; Head tracking moves the view but not the aim, so without this the ring\n"
        "; sits at the centre of the picture and stops marking the thing you\n"
        "; would interact with.\n"
        "Enabled=1\n"
        "; How far the aim trace reaches, in centimetres.\n"
        "TraceDistance=20000\n"
        "; Which trace channel the aim ray runs on (ETraceTypeQuery index).\n"
        "TraceChannel=%d\n\n"
        "[Flashlight]\n"
        "; Point the flashlight where you are looking. The game hangs the beam\n"
        "; off the mouse aim, so without this it keeps lighting whatever the\n"
        "; mouse points at while you look somewhere else.\n"
        "Enabled=1\n"
        "; How far the beam turns relative to your head. 1.5 leads the view,\n"
        "; 1.0 matches it, 0 pins the beam back on the mouse aim.\n"
        "Multiplier=%.1f\n\n"
        "[Hotkeys]\n"
        "; Virtual-key code for the yaw-mode toggle. Ctrl+Shift+H does the same\n"
        "; job and is not configurable.\n"
        "YawModeKey=0x%02X\n\n"
        "[Dev]\n"
        "; Ctrl+Shift+U / Ctrl+Shift+J cycle which GetPlayerViewPoint caller is\n"
        "; head-tracked. Only needed to re-confirm the render caller after a\n"
        "; game patch moves it.\n"
        "InjectHotkeys=0\n"
        "; List the live UMG widgets the reticle pass could move, to HeadTracking.log.\n"
        "WidgetDump=0\n",
        Config{}.udp_port,
        static_cast<double>(cameraunlock::PositionSettings{}.limit_y_down),
        Config{}.collision_enabled ? 1 : 0,
        static_cast<double>(Config{}.collision_radius),
        Config{}.collision_channel,
        static_cast<double>(Config{}.collision_release_smoothing),
        Config{}.aim_trace_channel,
        static_cast<double>(Config{}.flashlight.multiplier),
        kDefaultYawModeKey);
    std::fclose(f);
}

}  // namespace subliminal_ht
