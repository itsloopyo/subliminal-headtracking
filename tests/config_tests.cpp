// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

// The INI contract: the vertical position limits, and what a malformed value
// resolves to.
//
// The processor clamps y as [-limit_y_down, +limit_y], and limit_y_down carries
// its own default. An INI written before the LimitYDown key existed carries only
// LimitY, so LimitY has to reach both bounds or the player gets asymmetric travel
// with nothing in the log saying why.
//
// The validated keys go through one range check whose job is to reject a NaN -
// strtod parses "nan" and "inf" as valid floats, and a plain clamp passes a NaN
// through because both of its comparisons are false. A NaN smoothing reaches the
// FRotator written back through the camera hook and kills the view for the rest
// of the session. These cases pin what each malformed shape resolves to, so a
// later edit to the checker cannot quietly change it.

#include <cstdio>
#include <cstdlib>
#include <string>

#include <windows.h>

#include "config.h"

#include "cameraunlock/camera/lean_clamp.h"
#include "cameraunlock/data/position_settings.h"
#include "cameraunlock/math/smoothing_utils.h"

namespace {

int g_failures = 0;

void Check(bool ok, const char* what) {
    if (ok) return;
    std::printf("FAIL: %s\n", what);
    ++g_failures;
}

void CheckInt(int actual, int expected, const char* what) {
    if (actual == expected) return;
    std::printf("FAIL: %s (expected %d, got %d)\n", what, expected, actual);
    ++g_failures;
}

void CheckNear(float actual, float expected, const char* what) {
    if (actual >= expected - 1e-6f && actual <= expected + 1e-6f) return;
    std::printf("FAIL: %s (expected %.6f, got %.6f)\n", what,
                static_cast<double>(expected), static_cast<double>(actual));
    ++g_failures;
}

// IniReader reads through GetPrivateProfileString, which caches the file it last
// read, so every case gets a directory of its own under TEMP.
std::string WriteIni(const char* tag, const char* body) {
    char temp[MAX_PATH] = {};
    GetTempPathA(MAX_PATH, temp);
    const std::string dir = std::string(temp) + "subliminal_ht_config_" + tag;
    CreateDirectoryA(dir.c_str(), nullptr);

    const std::string path = dir + "\\HeadTracking.ini";
    FILE* f = nullptr;
    fopen_s(&f, path.c_str(), "w");
    if (f == nullptr) {
        std::printf("FAIL: could not write %s\n", path.c_str());
        ++g_failures;
        return dir;
    }
    std::fputs(body, f);
    std::fclose(f);
    return dir;
}

void LimitYReachesBothBoundsWhenLimitYDownIsAbsent() {
    subliminal_ht::Config raised;
    subliminal_ht::config_load(WriteIni("wide", "[Position]\nLimitY=0.40\n"), raised);
    CheckNear(raised.limit_y, 0.40f, "LimitY=0.40 raises the upward bound");
    CheckNear(raised.limit_y_down, 0.40f,
              "LimitY=0.40 raises the downward bound too, rather than leaving 0.20");

    subliminal_ht::Config tightened;
    subliminal_ht::config_load(WriteIni("tight", "[Position]\nLimitY=0.05\n"), tightened);
    CheckNear(tightened.limit_y, 0.05f, "LimitY=0.05 lowers the upward bound");
    CheckNear(tightened.limit_y_down, 0.05f, "LimitY=0.05 lowers the downward bound too");
}

void AnExplicitLimitYDownStillWins() {
    subliminal_ht::Config cfg;
    subliminal_ht::config_load(WriteIni("both", "[Position]\nLimitY=0.40\nLimitYDown=0.05\n"), cfg);
    CheckNear(cfg.limit_y, 0.40f, "LimitY is read");
    CheckNear(cfg.limit_y_down, 0.05f, "an explicit LimitYDown overrides the mirrored value");
}

// The numeric contract from the fleet doctrine, asserted as NUMBERS.
//
// Most of the checks in this file compare a parsed value against Config{}, which
// locks the routing ("a malformed value lands on the struct default") and says
// nothing at all about the value. A fat-fingered 0.04f in place of 0.40f for
// limit_z would pass every one of them. These are the numbers a player feels, so
// they are written out rather than derived.
void DefaultsMatchTheFleetContract() {
    const subliminal_ht::Config d;
    CheckInt(d.udp_port, 4242, "the default UDP port is the OpenTrack standard");
    CheckNear(d.yaw_sensitivity,   1.0f, "yaw sensitivity defaults to 1.0");
    CheckNear(d.pitch_sensitivity, 1.0f, "pitch sensitivity defaults to 1.0");
    CheckNear(d.roll_sensitivity,  1.0f, "roll sensitivity defaults to 1.0");
    CheckNear(d.position_sensitivity_x, 1.0f, "position sensitivity x defaults to 1.0");
    CheckNear(d.position_sensitivity_y, 1.0f, "position sensitivity y defaults to 1.0");
    CheckNear(d.position_sensitivity_z, 1.0f, "position sensitivity z defaults to 1.0");
    CheckNear(d.local_smoothing,  0.00f, "LocalSmoothing defaults to 0.0 - no floor");
    CheckNear(d.remote_smoothing, 0.15f, "RemoteSmoothing defaults to 0.15");
    CheckNear(d.limit_x,      0.30f, "LimitX defaults to 0.30m");
    CheckNear(d.limit_y,      0.20f, "LimitY defaults to 0.20m");
    CheckNear(d.limit_y_down, 0.20f, "LimitYDown defaults to 0.20m");
    CheckNear(d.limit_z,      0.40f, "LimitZ defaults to 0.40m forward");
    CheckNear(d.limit_z_back, 0.10f, "LimitZBack defaults to 0.10m");
    CheckNear(d.collision_release_smoothing, 0.90f,
              "CollisionReleaseSmoothing defaults to 0.9 - a 200ms time constant");
    // Against the parser's own floor, not a copy of the number: a default below
    // the accepted minimum logs a warning on every launch and silently runs at
    // the minimum instead of the default.
    Check(d.collision_radius >= subliminal_ht::kCollisionRadiusMin,
          "the shipped collision standoff is inside the range the parser accepts");
    Check(!d.inject_hotkeys, "the dev inject hotkeys are off by default");
    Check(!d.widget_dump, "the widget dump is off by default");
    CheckInt(d.yaw_mode_key, 0x22, "the yaw-mode key defaults to Page Down");
}

// The generated default INI must state the values the mod actually runs with.
// Half of them are formatted from a real default and half were written as
// literals, so this loads what was written back through the parser and compares
// the WHOLE struct - the drift this catches is a core-side tuning change that
// moves Config's default while the file the mod writes on a fresh install keeps
// quoting the old number.
void WrittenDefaultIniMatchesCoreLimitYDown() {
    char temp[MAX_PATH] = {};
    GetTempPathA(MAX_PATH, temp);
    const std::string dir = std::string(temp) + "subliminal_ht_config_written_default";
    CreateDirectoryA(dir.c_str(), nullptr);
    // config_write_default_if_missing is a no-op once the file exists, so a leftover
    // from a previous run of this test would hide a regression here.
    DeleteFileA((dir + "\\HeadTracking.ini").c_str());

    subliminal_ht::config_write_default_if_missing(dir);

    subliminal_ht::Config cfg;
    subliminal_ht::config_load(dir, cfg);
    const subliminal_ht::Config d;
    CheckNear(cfg.limit_y_down, cameraunlock::PositionSettings{}.limit_y_down,
              "the written default INI's LimitYDown matches core's PositionSettings default");
    CheckInt (cfg.udp_port,        d.udp_port,        "written INI: UdpPort");
    CheckNear(cfg.local_smoothing, d.local_smoothing, "written INI: LocalSmoothing");
    CheckNear(cfg.remote_smoothing,d.remote_smoothing,"written INI: RemoteSmoothing");
    CheckNear(cfg.position_sensitivity_x, d.position_sensitivity_x, "written INI: SensitivityX");
    CheckNear(cfg.position_sensitivity_y, d.position_sensitivity_y, "written INI: SensitivityY");
    CheckNear(cfg.position_sensitivity_z, d.position_sensitivity_z, "written INI: SensitivityZ");
    CheckNear(cfg.limit_x,         d.limit_x,         "written INI: LimitX");
    CheckNear(cfg.limit_y,         d.limit_y,         "written INI: LimitY");
    CheckNear(cfg.limit_z,         d.limit_z,         "written INI: LimitZ");
    CheckNear(cfg.limit_z_back,    d.limit_z_back,    "written INI: LimitZBack");
    CheckNear(cfg.collision_radius,d.collision_radius,"written INI: CollisionRadius");
    CheckInt (cfg.collision_channel, d.collision_channel, "written INI: CollisionChannel");
    CheckNear(cfg.collision_release_smoothing, d.collision_release_smoothing,
              "written INI: CollisionReleaseSmoothing");
    CheckNear(cfg.aim_trace_distance, d.aim_trace_distance, "written INI: TraceDistance");
    CheckInt (cfg.aim_trace_channel,  d.aim_trace_channel,  "written INI: TraceChannel");
    CheckInt (cfg.yaw_mode_key,       d.yaw_mode_key,       "written INI: YawModeKey");
    // These six are written as literals rather than formatted from a default,
    // so they are the only ones that CAN drift - which makes them the point of
    // this test rather than an afterthought.
    CheckNear(cfg.yaw_sensitivity,   d.yaw_sensitivity,   "written INI: YawSensitivity");
    CheckNear(cfg.pitch_sensitivity, d.pitch_sensitivity, "written INI: PitchSensitivity");
    CheckNear(cfg.roll_sensitivity,  d.roll_sensitivity,  "written INI: RollSensitivity");
    Check(cfg.invert_yaw   == d.invert_yaw,   "written INI: InvertYaw");
    Check(cfg.invert_pitch == d.invert_pitch, "written INI: InvertPitch");
    Check(cfg.invert_roll  == d.invert_roll,  "written INI: InvertRoll");
    Check(cfg.enable_on_startup  == d.enable_on_startup,  "written INI: EnableOnStartup");
    Check(cfg.world_space_yaw    == d.world_space_yaw,    "written INI: WorldSpaceYaw");
    Check(cfg.position_enabled   == d.position_enabled,   "written INI: Position/Enabled");
    Check(cfg.reticle_follows_aim== d.reticle_follows_aim,"written INI: Reticle/Enabled");
    Check(cfg.flashlight.follows_head == d.flashlight.follows_head,
          "written INI: Flashlight/Enabled");
    Check(cfg.collision_enabled  == d.collision_enabled,  "written INI: CollisionEnabled");
    Check(cfg.inject_hotkeys     == d.inject_hotkeys,     "written INI: InjectHotkeys");
    Check(cfg.widget_dump        == d.widget_dump,        "written INI: WidgetDump");
}

// A value that is not a finite number resolves to the key's own fallback, and
// the two smoothing keys have DIFFERENT fallbacks on purpose: a malformed
// RemoteSmoothing must not drop to the local default and leave a phone on WiFi
// running with no smoothing at all.
void NonFiniteValuesFallBackPerKey() {
    subliminal_ht::Config cfg;
    subliminal_ht::config_load(
        WriteIni("nonfinite",
                 "[Rotation]\nLocalSmoothing=nan\nRemoteSmoothing=inf\n"
                 "[Collision]\nCollisionRadius=nan\nCollisionReleaseSmoothing=nan\n"
                 "[Reticle]\nTraceDistance=nan\n"),
        cfg);
    CheckNear(cfg.local_smoothing,
              static_cast<float>(cameraunlock::math::kDefaultLocalSmoothing),
              "a non-finite LocalSmoothing falls back to the local default");
    CheckNear(cfg.remote_smoothing,
              static_cast<float>(cameraunlock::math::kDefaultRemoteSmoothing),
              "a non-finite RemoteSmoothing falls back to the remote default");
    // The struct default, so a malformed value and an absent one land in the
    // same place rather than on two different numbers.
    CheckNear(cfg.collision_radius, subliminal_ht::Config{}.collision_radius,
              "a non-finite CollisionRadius falls back to the struct default");
    CheckNear(cfg.collision_release_smoothing,
              cameraunlock::camera::LeanClampSettings{}.release_smoothing,
              "a non-finite CollisionReleaseSmoothing falls back to core's default");
    CheckNear(cfg.aim_trace_distance, 20000.0f,
              "a non-finite TraceDistance falls back to 20000");
}

// A finite value outside the range is held at the bound it crossed, not replaced
// by the fallback - the user asked for more than the range allows, and the
// nearest legal answer is what they meant.
void OutOfRangeValuesClampToTheBound() {
    subliminal_ht::Config high;
    subliminal_ht::config_load(
        WriteIni("high",
                 "[Rotation]\nLocalSmoothing=5.0\n"
                 "[Collision]\nCollisionRadius=9999\n"
                 "[Reticle]\nTraceDistance=99999999\n"),
        high);
    CheckNear(high.local_smoothing, 1.0f, "LocalSmoothing above 1.0 clamps to 1.0");
    CheckNear(high.collision_radius, 100.0f, "CollisionRadius clamps to 100");
    CheckNear(high.aim_trace_distance, 1000000.0f, "TraceDistance clamps to 1000000");

    subliminal_ht::Config low;
    subliminal_ht::config_load(
        WriteIni("low",
                 "[Rotation]\nLocalSmoothing=-3.0\n"
                 "[Collision]\nCollisionRadius=0.0\n"
                 "[Reticle]\nTraceDistance=1\n"),
        low);
    CheckNear(low.local_smoothing, 0.0f, "LocalSmoothing below 0.0 clamps to 0.0");
    // The floor is the engine's 10cm near clip distance: a standoff inside it
    // does not keep the wall drawn.
    // Above the 10cm near plane, not at it: geometry closer to the eye than the
    // near plane is culled, so a wall held at exactly that distance is still not
    // drawn and the lean still sees through it.
    CheckNear(low.collision_radius, subliminal_ht::kCollisionRadiusMin,
              "CollisionRadius clamps clear of the near plane");
    CheckNear(low.aim_trace_distance, 100.0f, "TraceDistance clamps to 100");
}

// Zero is a legal smoothing value and must survive untouched: the checker is
// validation, never a floor.
void AConfiguredZeroSmoothingSurvives() {
    subliminal_ht::Config cfg;
    subliminal_ht::config_load(
        WriteIni("zero", "[Rotation]\nLocalSmoothing=0.0\nRemoteSmoothing=0.0\n"), cfg);
    CheckNear(cfg.local_smoothing, 0.0f, "a configured LocalSmoothing of 0.0 stays 0.0");
    CheckNear(cfg.remote_smoothing, 0.0f, "a configured RemoteSmoothing of 0.0 stays 0.0");
}

// A YawModeKey that GetAsyncKeyState can never report would leave the toggle
// silently dead, so it resolves back to Page Down.
void AnImpossibleYawModeKeyFallsBackToPageDown() {
    subliminal_ht::Config out;
    subliminal_ht::config_load(WriteIni("vkhigh", "[Hotkeys]\nYawModeKey=0x1FF\n"), out);
    CheckInt(out.yaw_mode_key, subliminal_ht::kDefaultYawModeKey,
             "a YawModeKey above 0xFE falls back to Page Down");

    subliminal_ht::Config unparsed;
    subliminal_ht::config_load(WriteIni("vkjunk", "[Hotkeys]\nYawModeKey=notakey\n"), unparsed);
    CheckInt(unparsed.yaw_mode_key, subliminal_ht::kDefaultYawModeKey,
             "a YawModeKey that will not parse falls back to Page Down");

    subliminal_ht::Config kept;
    subliminal_ht::config_load(WriteIni("vkok", "[Hotkeys]\nYawModeKey=0x2D\n"), kept);
    CheckInt(kept.yaw_mode_key, 0x2D, "a legal YawModeKey is kept");
}

// Every float the INI can supply reaches the camera transform, so every one of
// them is range-checked. These were read raw for a while: a NaN sensitivity
// multiplied the pose into a NaN FRotator that was written back through the
// GetPlayerViewPoint hook for the rest of the session, with nothing in the log.
void SensitivitiesAreValidated() {
    subliminal_ht::Config nonfinite;
    subliminal_ht::config_load(
        WriteIni("sensnan", "[Rotation]\nYawSensitivity=nan\nRollSensitivity=inf\n"
                            "[Position]\nSensitivityZ=nan\n"),
        nonfinite);
    CheckNear(nonfinite.yaw_sensitivity, 1.0f, "a non-finite YawSensitivity falls back to 1.0");
    CheckNear(nonfinite.roll_sensitivity, 1.0f, "a non-finite RollSensitivity falls back to 1.0");
    CheckNear(nonfinite.position_sensitivity_z, 1.0f,
              "a non-finite position SensitivityZ falls back to 1.0");

    // A European decimal comma is the typo this catches: strtod parses the
    // prefix, so "2,5" used to read back as 2.0 - inside every range, and
    // silently not what the user typed.
    subliminal_ht::Config comma;
    subliminal_ht::config_load(WriteIni("senscomma", "[Rotation]\nPitchSensitivity=2,5\n"), comma);
    CheckNear(comma.pitch_sensitivity, 1.0f,
              "a comma-decimal PitchSensitivity is refused rather than read as 2.0");

    // A negative sensitivity is a legitimate way to invert an axis, so it is
    // kept rather than clamped away.
    subliminal_ht::Config negative;
    subliminal_ht::config_load(WriteIni("sensneg", "[Rotation]\nYawSensitivity=-1.5\n"), negative);
    CheckNear(negative.yaw_sensitivity, -1.5f, "a negative YawSensitivity is kept");
}

// A negative travel limit is not merely odd: PositionProcessor clamps as
// Clamp(v, -limit, +limit), which with a negative limit returns the lower bound
// for every input and pins the eye at a fixed offset instead of freeing it.
void PositionLimitsAreValidated() {
    subliminal_ht::Config negative;
    subliminal_ht::config_load(WriteIni("limneg", "[Position]\nLimitX=-0.30\n"), negative);
    CheckNear(negative.limit_x, 0.0f, "a negative LimitX clamps to zero travel");

    subliminal_ht::Config nonfinite;
    subliminal_ht::config_load(WriteIni("limnan", "[Position]\nLimitZ=nan\nLimitY=inf\n"), nonfinite);
    CheckNear(nonfinite.limit_z, subliminal_ht::Config{}.limit_z,
              "a non-finite LimitZ falls back to the shipped default");
    CheckNear(nonfinite.limit_y, subliminal_ht::Config{}.limit_y,
              "a non-finite LimitY falls back to the shipped default");
    // LimitYDown still mirrors whatever LimitY resolved to.
    CheckNear(nonfinite.limit_y_down, subliminal_ht::Config{}.limit_y,
              "LimitYDown mirrors the validated LimitY rather than the raw one");

    subliminal_ht::Config kept;
    subliminal_ht::config_load(WriteIni("limok", "[Position]\nLimitX=0.45\n"), kept);
    CheckNear(kept.limit_x, 0.45f, "a legal LimitX is kept");
}

// Both traces write the channel into a one-byte parameter slot, so an index
// outside ETraceTypeQuery's 0-31 range is truncated on the way in - 256 becomes
// channel 0, -1 becomes 255 - and the trace silently runs somewhere else.
void TraceChannelsAreValidated() {
    subliminal_ht::Config out_of_range;
    subliminal_ht::config_load(
        WriteIni("chan", "[Collision]\nCollisionChannel=256\n[Reticle]\nTraceChannel=-1\n"),
        out_of_range);
    CheckInt(out_of_range.collision_channel, subliminal_ht::Config{}.collision_channel,
             "a CollisionChannel above 31 falls back to the default");
    CheckInt(out_of_range.aim_trace_channel, subliminal_ht::Config{}.aim_trace_channel,
             "a negative TraceChannel falls back to the default");

    subliminal_ht::Config kept;
    subliminal_ht::config_load(
        WriteIni("chanok", "[Collision]\nCollisionChannel=2\n[Reticle]\nTraceChannel=1\n"), kept);
    CheckInt(kept.collision_channel, 2, "a legal CollisionChannel is kept");
    CheckInt(kept.aim_trace_channel, 1, "a legal TraceChannel is kept");
}

// Turning the flashlight off has to reach the mod, because the beam is the one
// thing this mod moves that the game also lights the world with: a player who
// wants it back on the mouse aim has no other way to say so.
void FlashlightKeyIsRead() {
    subliminal_ht::Config off;
    subliminal_ht::config_load(WriteIni("floff", "[Flashlight]\nEnabled=0\n"), off);
    Check(!off.flashlight.follows_head, "Flashlight/Enabled=0 hands the beam back");

    subliminal_ht::Config on;
    subliminal_ht::config_load(WriteIni("flon", "[Flashlight]\nEnabled=1\n"), on);
    Check(on.flashlight.follows_head, "Flashlight/Enabled=1 keeps the beam on the head");

    subliminal_ht::Config absent;
    subliminal_ht::config_load(WriteIni("flabs", "[General]\nEnableOnStartup=1\n"), absent);
    Check(absent.flashlight.follows_head == subliminal_ht::Config{}.flashlight.follows_head,
          "an absent Flashlight section leaves the struct default");
}

// Ctrl and Shift are what the chord guard tests, so a toggle bound to one either
// never fires or fires on every chord the mod watches.
void AModifierIsRefusedAsYawModeKey() {
    subliminal_ht::Config shift;
    subliminal_ht::config_load(WriteIni("vkshift", "[Hotkeys]\nYawModeKey=0x10\n"), shift);
    CheckInt(shift.yaw_mode_key, subliminal_ht::kDefaultYawModeKey,
             "YawModeKey=Shift falls back to Page Down");
}

// A port outside the range core accepts resolves to the OpenTrack default rather
// than a bind that can never succeed.
void AnOutOfRangeUdpPortFallsBackToTheDefault() {
    subliminal_ht::Config low;
    subliminal_ht::config_load(WriteIni("portlow", "[Network]\nUdpPort=80\n"), low);
    CheckInt(low.udp_port, 4242, "a UdpPort below 1024 falls back to 4242");

    subliminal_ht::Config kept;
    subliminal_ht::config_load(WriteIni("portok", "[Network]\nUdpPort=6040\n"), kept);
    CheckInt(kept.udp_port, 6040, "a legal UdpPort is kept");
}

}  // namespace

int main() {
    DefaultsMatchTheFleetContract();
    LimitYReachesBothBoundsWhenLimitYDownIsAbsent();
    AnExplicitLimitYDownStillWins();
    WrittenDefaultIniMatchesCoreLimitYDown();
    NonFiniteValuesFallBackPerKey();
    OutOfRangeValuesClampToTheBound();
    AConfiguredZeroSmoothingSurvives();
    AnImpossibleYawModeKeyFallsBackToPageDown();
    AModifierIsRefusedAsYawModeKey();
    SensitivitiesAreValidated();
    PositionLimitsAreValidated();
    TraceChannelsAreValidated();
    FlashlightKeyIsRead();
    AnOutOfRangeUdpPortFallsBackToTheDefault();

    if (g_failures != 0) {
        std::printf("%d check(s) failed\n", g_failures);
        return EXIT_FAILURE;
    }
    std::printf("all checks passed\n");
    return EXIT_SUCCESS;
}
