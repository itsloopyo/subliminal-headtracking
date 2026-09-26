// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

// Frozen. See legacy_config.h.

#include "legacy_config.h"

#include <cerrno>
#include <cstdlib>
#include <string>
#include <vector>

#include "logging.h"

#include "cameraunlock/config/ini_reader.h"
#include "cameraunlock/config/value_guards.h"
#include "cameraunlock/protocol/port_utils.h"

namespace subliminal_ht::legacy {

namespace {

constexpr int kVkToggleTracking = 0x23;  // VK_END
constexpr int kVkCycleMode = 0x21;       // VK_PRIOR (Page Up)
constexpr int kDefaultYawModeKey = 0x22; // VK_NEXT (Page Down)

// The collision standoff's floor is the engine's 10cm near clip distance plus
// one; past a metre the lean is rationed by furniture the player is nowhere near.
constexpr float kCollisionRadiusMin = 11.0f;
constexpr float kCollisionRadiusMax = 100.0f;
constexpr float kCollisionRadiusDefault = 20.0f;

constexpr float kMaxSensitivity = 100.0f;
constexpr float kMaxPositionLimit = 10.0f;

// Per-key fallbacks for a rejected smoothing value. They differ on purpose: a
// malformed RemoteSmoothing must not drop back to the LOCAL default.
constexpr float kLocalSmoothingFallback = 0.0f;
constexpr float kRemoteSmoothingFallback = 0.15f;
constexpr float kReleaseSmoothingFallback = 0.9f;

constexpr float kLightMultiplierDefault = 1.5f;
constexpr float kLightMultiplierMax = 5.0f;

constexpr float kTraceDistanceMin = 100.0f;
constexpr float kTraceDistanceMax = 1000000.0f;
constexpr float kTraceDistanceDefault = 20000.0f;

constexpr cameraunlock::config::LogSink kLogSink = &Log::Line;

// Read a float and hold it to a range: a value that does not parse whole or is
// not finite takes the fallback, and a finite one outside the range is clamped
// to the bound, with a log line either way.
float read_ranged(const cameraunlock::IniReader& ini, const char* section,
                  const char* key, float lo, float hi, float fallback) {
    return cameraunlock::config::ReadFloatChecked(ini, section, key, fallback,
                                                  lo, hi, kLogSink);
}

// ETraceTypeQuery is TraceTypeQuery1..TraceTypeQuery32.
constexpr int kMaxTraceTypeQuery = 31;

// Read through the raw token, so a value that does not parse whole, or one
// outside 0-31, takes the fallback with a log line. An absent key takes it
// silently.
int read_trace_channel(const cameraunlock::IniReader& ini, const char* section,
                       const char* key, int fallback) {
    const std::string raw = cameraunlock::config::ReadRawValue(ini, section, key);
    if (raw.empty()) return fallback;

    errno = 0;
    char* end = nullptr;
    const long v = std::strtol(raw.c_str(), &end, 10);
    const bool parsed = end != raw.c_str() && *end == '\0' && errno != ERANGE;

    if (!parsed || v < 0 || v > kMaxTraceTypeQuery) {
        Log::Line("config: [%s] %s '%s' is not an ETraceTypeQuery index (0-%d), using %d",
            section, key, raw.c_str(), kMaxTraceTypeQuery, fallback);
        return fallback;
    }
    return static_cast<int>(v);
}

// A code core's guard refuses (outside 0x01-0xFE, or a modifier), or one of the
// mod's other two nav keys, is replaced by Page Down.
int sanitize_vk(int v) {
    if (!cameraunlock::config::IsBindableVirtualKey(v)) {
        Log::Line("config: [Hotkeys] YawModeKey 0x%02X is not a key this mod can watch, "
                  "using 0x%02X (Page Down)", v, kDefaultYawModeKey);
        return kDefaultYawModeKey;
    }
    if (v == kVkToggleTracking || v == kVkCycleMode) {
        Log::Line("config: [Hotkeys] YawModeKey 0x%02X is already the %s binding - one "
                  "press would fire both actions. Using 0x%02X (Page Down)",
                  v, v == kVkToggleTracking ? "tracking toggle" : "mode cycle",
                  kDefaultYawModeKey);
        return kDefaultYawModeKey;
    }
    return v;
}

}  // namespace

bool Load(const std::string& ini_path, Config& out) {
    cameraunlock::IniReader ini;
    if (!ini.Open(ini_path)) return false;

    bool port_valid = false;
    out.udp_port = cameraunlock::NormalizeUdpPort(
        ini.ReadInt("Network", "UdpPort", out.udp_port), 4242, port_valid);
    if (!port_valid)
        Log::Line("config: [Network] UdpPort is outside 1024-65535, using 4242");

    out.enable_on_startup  = ini.ReadBool ("General",  "EnableOnStartup",  out.enable_on_startup);
    out.world_space_yaw    = ini.ReadBool ("General",  "WorldSpaceYaw",    out.world_space_yaw);

    out.yaw_sensitivity    = read_ranged(ini, "Rotation", "YawSensitivity",
        -kMaxSensitivity, kMaxSensitivity, out.yaw_sensitivity);
    out.pitch_sensitivity  = read_ranged(ini, "Rotation", "PitchSensitivity",
        -kMaxSensitivity, kMaxSensitivity, out.pitch_sensitivity);
    out.roll_sensitivity   = read_ranged(ini, "Rotation", "RollSensitivity",
        -kMaxSensitivity, kMaxSensitivity, out.roll_sensitivity);
    out.invert_yaw         = ini.ReadBool ("Rotation", "InvertYaw",        out.invert_yaw);
    out.invert_pitch       = ini.ReadBool ("Rotation", "InvertPitch",      out.invert_pitch);
    out.invert_roll        = ini.ReadBool ("Rotation", "InvertRoll",       out.invert_roll);

    out.local_smoothing    = read_ranged(ini, "Rotation", "LocalSmoothing",
        0.0f, 1.0f, kLocalSmoothingFallback);
    out.remote_smoothing   = read_ranged(ini, "Rotation", "RemoteSmoothing",
        0.0f, 1.0f, kRemoteSmoothingFallback);

    out.position_enabled   = ini.ReadBool ("Position", "Enabled",          out.position_enabled);
    out.position_sensitivity_x = read_ranged(ini, "Position", "SensitivityX",
        -kMaxSensitivity, kMaxSensitivity, out.position_sensitivity_x);
    out.position_sensitivity_y = read_ranged(ini, "Position", "SensitivityY",
        -kMaxSensitivity, kMaxSensitivity, out.position_sensitivity_y);
    out.position_sensitivity_z = read_ranged(ini, "Position", "SensitivityZ",
        -kMaxSensitivity, kMaxSensitivity, out.position_sensitivity_z);
    out.limit_x            = read_ranged(ini, "Position", "LimitX",
        0.0f, kMaxPositionLimit, out.limit_x);
    out.limit_y            = read_ranged(ini, "Position", "LimitY",
        0.0f, kMaxPositionLimit, out.limit_y);
    // Falls back to whatever LimitY resolved to, not to the struct default.
    out.limit_y_down       = read_ranged(ini, "Position", "LimitYDown",
        0.0f, kMaxPositionLimit, out.limit_y);
    out.limit_z            = read_ranged(ini, "Position", "LimitZ",
        0.0f, kMaxPositionLimit, out.limit_z);
    out.limit_z_back       = read_ranged(ini, "Position", "LimitZBack",
        0.0f, kMaxPositionLimit, out.limit_z_back);

    out.collision_enabled  = ini.ReadBool ("Collision", "CollisionEnabled", out.collision_enabled);
    // A malformed value and an absent one both land on the struct default.
    out.collision_radius   = read_ranged(ini, "Collision", "CollisionRadius",
        kCollisionRadiusMin, kCollisionRadiusMax, kCollisionRadiusDefault);
    out.collision_channel  = read_trace_channel(ini, "Collision", "CollisionChannel",
        out.collision_channel);
    out.collision_release_smoothing = read_ranged(ini, "Collision",
        "CollisionReleaseSmoothing", 0.0f, 1.0f, kReleaseSmoothingFallback);

    out.reticle_follows_aim = ini.ReadBool("Reticle", "Enabled",           out.reticle_follows_aim);
    out.aim_trace_distance  = read_ranged(ini, "Reticle", "TraceDistance",
        kTraceDistanceMin, kTraceDistanceMax, kTraceDistanceDefault);
    out.aim_trace_channel   = read_trace_channel(ini, "Reticle", "TraceChannel",
        out.aim_trace_channel);

    out.flashlight_follows_head = ini.ReadBool("Flashlight", "Enabled",
        out.flashlight_follows_head);
    out.flashlight_multiplier = read_ranged(ini, "Flashlight", "Multiplier",
        0.0f, kLightMultiplierMax, kLightMultiplierDefault);

    out.yaw_mode_key       = sanitize_vk(
        ini.ReadHex("Hotkeys", "YawModeKey", out.yaw_mode_key));

    out.inject_hotkeys     = ini.ReadBool ("Dev",      "InjectHotkeys",    out.inject_hotkeys);
    out.widget_dump        = ini.ReadBool ("Dev",      "WidgetDump",       out.widget_dump);
    return true;
}

std::vector<Key> ReadKeys() {
    return {
        {"Network", "UdpPort"},
        {"General", "EnableOnStartup"},
        {"General", "WorldSpaceYaw"},
        {"Rotation", "YawSensitivity"},
        {"Rotation", "PitchSensitivity"},
        {"Rotation", "RollSensitivity"},
        {"Rotation", "InvertYaw"},
        {"Rotation", "InvertPitch"},
        {"Rotation", "InvertRoll"},
        {"Rotation", "LocalSmoothing"},
        {"Rotation", "RemoteSmoothing"},
        {"Position", "Enabled"},
        {"Position", "SensitivityX"},
        {"Position", "SensitivityY"},
        {"Position", "SensitivityZ"},
        {"Position", "LimitX"},
        {"Position", "LimitY"},
        {"Position", "LimitYDown"},
        {"Position", "LimitZ"},
        {"Position", "LimitZBack"},
        {"Collision", "CollisionEnabled"},
        {"Collision", "CollisionRadius"},
        {"Collision", "CollisionChannel"},
        {"Collision", "CollisionReleaseSmoothing"},
        {"Reticle", "Enabled"},
        {"Reticle", "TraceDistance"},
        {"Reticle", "TraceChannel"},
        {"Flashlight", "Enabled"},
        {"Flashlight", "Multiplier"},
        {"Hotkeys", "YawModeKey"},
        {"Dev", "InjectHotkeys"},
        {"Dev", "WidgetDump"},
    };
}

}  // namespace subliminal_ht::legacy
