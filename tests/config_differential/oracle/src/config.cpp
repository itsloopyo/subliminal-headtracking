// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "config.h"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <windows.h>

#include "cameraunlock/config/ini_reader.h"
#include "cameraunlock/config/value_guards.h"
#include "cameraunlock/protocol/port_utils.h"
#include "logging.h"

namespace subliminal_ht {

namespace {

constexpr const char* kIniName = "HeadTracking.ini";

// The floor is kCollisionRadiusMin in config.h. Past a metre the lean is
// rationed by furniture the player is nowhere near.
constexpr float kCollisionRadiusMax = 100.0f;

// Core's bounds for the two kinds of value the mod reads. Named here so the
// call sites read as ranges rather than as core's spelling of them.
constexpr float kMaxSensitivity = cameraunlock::config::kMaxSensitivity;
constexpr float kMaxPositionLimit = cameraunlock::config::kMaxPositionLimit;

// Per-key fallbacks for a rejected smoothing value. They differ on purpose: a
// malformed RemoteSmoothing must not drop back to the LOCAL default, which would
// leave a phone on WiFi running with no smoothing at all on raw network jitter.
constexpr float kLocalSmoothingFallback =
    static_cast<float>(cameraunlock::math::kDefaultLocalSmoothing);
constexpr float kRemoteSmoothingFallback =
    static_cast<float>(cameraunlock::math::kDefaultRemoteSmoothing);

std::string ini_path(const std::string& exe_dir) {
    return exe_dir + "\\" + kIniName;
}

// The mod's log, in the shape core's guards take their diagnostic sink in.
constexpr cameraunlock::config::LogSink kLogSink = &Log::Line;

// Read a float and hold it to a range.
//
// Core's ReadFloatChecked does three things IniReader::ReadFloat does not, and
// every one of them is a way a typo reaches the camera transform:
//
//   - strtod parses "nan" and "inf" as perfectly valid floats, and a plain
//     clamp does NOT reject a NaN because both of its comparisons are false. A
//     NaN travels into CalculateSmoothingFactor, skips that function's own speed
//     clamp for the same reason, and poisons the FRotator and FVector written
//     back through the GetPlayerViewPoint hook for the rest of the session, with
//     nothing in the log to explain the dead camera.
//   - strtod parses a PREFIX, so "YawSensitivity=2,5" - a European decimal
//     comma - reads back as 2.0. That is inside every range, so it passes
//     silently while doing something the user did not ask for.
//   - GetPrivateProfileString does not strip an inline comment, so the whole
//     token has to be taken off before the number is parsed.
//
// This is validation, never a floor: a finite value inside the range is
// returned untouched, so a deliberately configured 0.0 stays 0.0.
float read_ranged(const cameraunlock::IniReader& ini, const char* section,
                  const char* key, float lo, float hi, float fallback) {
    return cameraunlock::config::ReadFloatChecked(ini, section, key, fallback,
                                                  lo, hi, kLogSink);
}

// ETraceTypeQuery is TraceTypeQuery1..TraceTypeQuery32, so 0..31 is the whole
// range the engine defines. Both traces write the index into a one-byte
// parameter slot, so a number outside it is truncated on the way in: 256 becomes
// channel 0 and -1 becomes 255, either silently running the trace on a channel
// the user did not ask for or handing the engine an index its own table does not
// define. Neither says anything in the log, and both read as "the clamp does not
// fire" or "the reticle sits still".
constexpr int kMaxTraceTypeQuery = 31;

// Read through the RAW token rather than IniReader::ReadInt. ReadInt is
// GetPrivateProfileIntA, which core's own header records as the one reader that
// returns 0 rather than defaultValue on a present-but-unparseable value. Zero is
// a legal channel, so `CollisionChannel=Visibility` would resolve to
// TraceTypeQuery1 with the range check satisfied and nothing in the log - the
// sweep silently running on a channel the user did not ask for. Every float key
// in this file was moved onto ReadFloatChecked for exactly this; these two were
// left on the reader that is worse than the ones that were replaced.
int read_trace_channel(const cameraunlock::IniReader& ini, const char* section,
                       const char* key, int fallback) {
    const std::string raw = cameraunlock::config::ReadRawValue(ini, section, key);
    if (raw.empty()) return fallback;   // absent: not a fault, and not worth a line

    errno = 0;
    char* end = nullptr;
    const long v = std::strtol(raw.c_str(), &end, 10);
    // The whole token has to be the number. strtol stops at the first character
    // it cannot use, so "1cm" and "0x8" would otherwise pass as 1 and 0. There is
    // no trailing-whitespace skip: core's ReadRawValue returns a trimmed token,
    // so an all-whitespace value comes back empty and has already returned.
    const bool parsed = end != raw.c_str() && *end == '\0' && errno != ERANGE;

    if (!parsed || v < 0 || v > kMaxTraceTypeQuery) {
        Log::Line("config: [%s] %s '%s' is not an ETraceTypeQuery index (0-%d), using %d",
            section, key, raw.c_str(), kMaxTraceTypeQuery, fallback);
        return fallback;
    }
    return static_cast<int>(v);
}

// A virtual-key code the poller can never see fire leaves the toggle silently
// dead, and the only clue is a hotkey that does nothing. Core's guard refuses
// two classes: codes outside the 0x01-0xFE range GetAsyncKeyState defines, and
// the modifiers themselves - Ctrl and Shift are what the chord guard tests, so
// an action bound to one either never fires or fires on every chord.
int sanitize_vk(int v) {
    if (!cameraunlock::config::IsBindableVirtualKey(v)) {
        Log::Line("config: [Hotkeys] YawModeKey 0x%02X is not a key this mod can watch, "
                  "using 0x%02X (Page Down)", v, kDefaultYawModeKey);
        return kDefaultYawModeKey;
    }
    // The poller keeps one edge-detected entry per registration, so binding this
    // to a key the mod already owns fires BOTH actions on one press: YawModeKey=0x23
    // would flip yaw mode every time End toggled tracking. Core's guard cannot
    // catch that - it knows nothing about this mod's other bindings.
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

void config_load(const std::string& exe_dir, Config& out) {
    cameraunlock::IniReader ini;
    if (!ini.Open(ini_path(exe_dir))) return;

    bool port_valid = false;
    out.udp_port = cameraunlock::NormalizeUdpPort(
        ini.ReadInt("Network", "UdpPort", out.udp_port), 4242, port_valid);
    if (!port_valid)
        Log::Line("config: [Network] UdpPort is outside 1024-65535, using 4242");

    out.enable_on_startup  = ini.ReadBool ("General",  "EnableOnStartup",  out.enable_on_startup);
    out.world_space_yaw    = ini.ReadBool ("General",  "WorldSpaceYaw",    out.world_space_yaw);

    // Sensitivity carries a sign as well as a magnitude - a negative one is a
    // legitimate way to invert an axis without touching the Invert flags - so
    // the only values refused are the ones that reach the camera as garbage.
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
    // A NEGATIVE travel limit is not merely a strange number: PositionProcessor
    // clamps as Clamp(v, -limit, +limit), and with a negative limit that returns
    // the lower bound for every input, pinning the eye at a fixed offset instead
    // of freeing it. The floor is zero, which is the honest "no travel on this
    // axis" the user asked for.
    out.limit_x            = read_ranged(ini, "Position", "LimitX",
        0.0f, kMaxPositionLimit, out.limit_x);
    out.limit_y            = read_ranged(ini, "Position", "LimitY",
        0.0f, kMaxPositionLimit, out.limit_y);
    // Falls back to whatever LimitY resolved to, not to the struct default: a
    // config that sets only LimitY would otherwise keep 0.20 m of downward
    // travel while the upward budget moved, with nothing saying the key was
    // half-effective.
    out.limit_y_down       = read_ranged(ini, "Position", "LimitYDown",
        0.0f, kMaxPositionLimit, out.limit_y);
    out.limit_z            = read_ranged(ini, "Position", "LimitZ",
        0.0f, kMaxPositionLimit, out.limit_z);
    out.limit_z_back       = read_ranged(ini, "Position", "LimitZBack",
        0.0f, kMaxPositionLimit, out.limit_z_back);

    out.collision_enabled  = ini.ReadBool ("Collision", "CollisionEnabled", out.collision_enabled);
    // The floor is the engine's near clip distance, not 1: a standoff smaller
    // than the near plane stops the eye short of the wall and the wall is culled
    // anyway, which is the same complaint with extra steps. The fallback is the
    // struct default rather than a second number, so a malformed value and an
    // absent one land in the same place.
    out.collision_radius   = read_ranged(ini, "Collision", "CollisionRadius",
        kCollisionRadiusMin, kCollisionRadiusMax, Config{}.collision_radius);
    out.collision_channel  = read_trace_channel(ini, "Collision", "CollisionChannel",
        out.collision_channel);
    out.collision_release_smoothing = read_ranged(ini, "Collision",
        "CollisionReleaseSmoothing", 0.0f, 1.0f,
        cameraunlock::camera::LeanClampSettings{}.release_smoothing);

    out.reticle_follows_aim = ini.ReadBool("Reticle", "Enabled",           out.reticle_follows_aim);
    out.aim_trace_distance  = read_ranged(ini, "Reticle", "TraceDistance",
        100.0f, 1000000.0f, 20000.0f);
    out.aim_trace_channel   = read_trace_channel(ini, "Reticle", "TraceChannel",
        out.aim_trace_channel);

    out.flashlight.follows_head = ini.ReadBool("Flashlight", "Enabled",
        out.flashlight.follows_head);
    // Zero is inside the range and is a real request to pin the beam to the aim,
    // so the floor is 0 and not a small positive number. Out of range is
    // rejected rather than clamped - running at 5 while the file says 8 is a
    // setting that does not do what it says.
    out.flashlight.multiplier = read_ranged(ini, "Flashlight", "Multiplier",
        0.0f, cameraunlock::effects::kMaxLightMultiplier,
        cameraunlock::effects::kDefaultLightMultiplier);

    out.yaw_mode_key       = sanitize_vk(
        ini.ReadHex("Hotkeys", "YawModeKey", out.yaw_mode_key));

    out.inject_hotkeys     = ini.ReadBool ("Dev",      "InjectHotkeys",    out.inject_hotkeys);
    out.widget_dump        = ini.ReadBool ("Dev",      "WidgetDump",       out.widget_dump);
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
