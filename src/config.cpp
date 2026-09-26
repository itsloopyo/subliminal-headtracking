// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "config.h"

#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "legacy_config/legacy_config.h"
#include "logging.h"

#include "cameraunlock/config/hotkey_codec.h"
#include "cameraunlock/config/value_codecs.h"
#include "cameraunlock/input/key_bindings.h"

namespace subliminal_ht::config {

namespace {

namespace cfg = ::cameraunlock::config;
using cfg::schema::Concept;
using ::cameraunlock::input::FormatKeyBindings;
using ::cameraunlock::input::KeyModifiers;

constexpr const wchar_t* kIniName = L"CameraUnlock.ini";
constexpr const wchar_t* kLegacyIniName = L"HeadTracking.ini";

// data/games.json's display_name for subliminal.
constexpr const char* kDisplayName = "Subliminal";

// The aim trace's reach, the bounds every earlier build held it to.
constexpr double kMinAimTraceDistance = 100.0;
constexpr double kMaxAimTraceDistance = 1000000.0;

// ETraceTypeQuery is TraceTypeQuery1..TraceTypeQuery32.
constexpr int kMaxTraceChannel = 31;

constexpr KeyModifiers kChord = KeyModifiers::kCtrl | KeyModifiers::kShift;

// The keys every build before the canonical format bound in code rather than in
// the file.
constexpr int kVkEnd = 0x23;
constexpr int kVkPageUp = 0x21;
constexpr int kVkY = 0x59;
constexpr int kVkG = 0x47;
constexpr int kVkH = 0x48;
constexpr int kVkU = 0x55;
constexpr int kVkJ = 0x4A;

std::unique_ptr<cfg::ConfigOwner<Config>> g_owner;

void Save(const char* rows, const std::function<void(Config&)>& change) {
    // No owner when the bootstrap could not read the game's folder.
    if (!g_owner) {
        Log::Line("config: %s not saved: CameraUnlock.ini has no known folder this session", rows);
        return;
    }
    const cfg::ConfigSaveResult result = g_owner->Save(change);
    if (result.status != cfg::ConfigSaveStatus::Saved) {
        Log::Line("config: %s %s: %s", rows, cfg::ConfigSaveStatusName(result.status), result.reason.c_str());
    }
    for (const std::string& line : result.log) Log::Line("config: %s", line.c_str());
}

cfg::ImportResult RunImport(const cfg::LegacyInput& input, Config& out) {
    // Every earlier build opened HeadTracking.ini by its ANSI path, and the
    // frozen reader does the same. Where it finds no file, the published build
    // ran on its defaults.
    legacy::Config read;
    const bool present = legacy::Load(input.ansi_path, read);

    std::vector<cfg::DroppedValue> dropped;
    std::vector<cfg::PoseShapingValue> pose_shaping;

    // Every sensitivity and inversion shipped at identity, and the position
    // offset is already converted to the game's centimetres in code, so nothing
    // folds: the mod applies the pose as the tracker sends it, and a value the
    // player changed is dropped.
    const auto shaping = [&](auto value, auto shipped, const char* section, const char* key) {
        cfg::LegacyPoseShaping(value, shipped, section, key, pose_shaping, dropped);
    };
    shaping(read.yaw_sensitivity, 1.0f, "Rotation", "YawSensitivity");
    shaping(read.pitch_sensitivity, 1.0f, "Rotation", "PitchSensitivity");
    shaping(read.roll_sensitivity, 1.0f, "Rotation", "RollSensitivity");
    shaping(read.invert_yaw, false, "Rotation", "InvertYaw");
    shaping(read.invert_pitch, false, "Rotation", "InvertPitch");
    shaping(read.invert_roll, false, "Rotation", "InvertRoll");
    shaping(read.position_sensitivity_x, 1.0f, "Position", "SensitivityX");
    shaping(read.position_sensitivity_y, 1.0f, "Position", "SensitivityY");
    shaping(read.position_sensitivity_z, 1.0f, "Position", "SensitivityZ");

    // The reader keeps the port inside 1024-65535, every float finite and inside
    // a range the canonical rows hold, and both trace channels inside 0-31, so
    // each carries over as it is.
    out.udp_port = read.udp_port;
    out.enable_on_startup = read.enable_on_startup;
    out.world_space_yaw = read.world_space_yaw;
    out.local_smoothing = read.local_smoothing;
    out.remote_smoothing = read.remote_smoothing;
    out.position_limit_x = read.limit_x;
    out.position_limit_y = read.limit_y;
    out.position_limit_y_down = read.limit_y_down;
    out.position_limit_z = read.limit_z;
    out.position_limit_z_back = read.limit_z_back;
    out.collision_enabled = read.collision_enabled;
    out.collision_margin = read.collision_radius;
    out.collision_channel = read.collision_channel;
    out.collision_release_smoothing = read.collision_release_smoothing;
    out.aim_trace_distance = read.aim_trace_distance;
    out.aim_trace_channel = read.aim_trace_channel;
    out.light_follows_head = read.flashlight_follows_head;
    out.light_multiplier = read.flashlight_multiplier;
    out.widget_dump = read.widget_dump;

    // [Position] Enabled chose the startup mode and nothing else: the cycle
    // reached every mode either way.
    const cameraunlock::TrackingModeChannels channels = cameraunlock::EncodeTrackingMode(
        read.position_enabled ? cameraunlock::TrackingMode::RotationAndPosition
                              : cameraunlock::TrackingMode::RotationOnly);
    out.rotation_enabled = channels.rotation_enabled;
    out.position_enabled = channels.position_enabled;

    // The crosshair ring now always follows the aim (approved change reticle).
    if (!read.reticle_follows_aim) dropped.push_back({cfg::DropRule::Reticle, "Reticle", "Enabled", "false"});

    // End, Page Up and the Ctrl+Shift chords were bound in code; only the yaw
    // key was in the file, and the reader keeps it bindable. The inject-mode
    // chords were bound only with [Dev] InjectHotkeys on.
    out.toggle_key = FormatKeyBindings({{KeyModifiers::kNone, kVkEnd}, {kChord, kVkY}});
    out.cycle_tracking_mode_key = FormatKeyBindings({{KeyModifiers::kNone, kVkPageUp}, {kChord, kVkG}});
    out.yaw_mode_key = cfg::LegacyVirtualKeyToBindings(read.yaw_mode_key, "Hotkeys", "YawModeKey", dropped) + ", " +
                       FormatKeyBindings({{kChord, kVkH}});
    out.inject_next_key = read.inject_hotkeys ? FormatKeyBindings({{kChord, kVkU}}) : std::string();
    out.inject_previous_key = read.inject_hotkeys ? FormatKeyBindings({{kChord, kVkJ}}) : std::string();

    return present ? cfg::ImportResult::Imported(std::move(dropped), std::move(pose_shaping))
                   : cfg::ImportResult::Absent(std::move(dropped), std::move(pose_shaping));
}

}  // namespace

cfg::ConfigTable<Config> Table() {
    cfg::ConfigTable<Config> table;
    table.Concept<Concept::UdpPort>(&Config::udp_port)
        .Concept<Concept::EnableOnStartup>(&Config::enable_on_startup)
        .Concept<Concept::WorldSpaceYaw>(&Config::world_space_yaw)
        .Writable()
        .Concept<Concept::RotationEnabled>(&Config::rotation_enabled)
        .Writable()
        .Concept<Concept::LocalSmoothing>(&Config::local_smoothing)
        .Concept<Concept::RemoteSmoothing>(&Config::remote_smoothing)
        .Concept<Concept::PositionEnabled>(&Config::position_enabled)
        .Writable()
        .Concept<Concept::PositionLimitX>(&Config::position_limit_x)
        .Concept<Concept::PositionLimitY>(&Config::position_limit_y)
        .Concept<Concept::PositionLimitYDown>(&Config::position_limit_y_down)
        .Concept<Concept::PositionLimitZ>(&Config::position_limit_z)
        .Concept<Concept::PositionLimitZBack>(&Config::position_limit_z_back)
        .Concept<Concept::CollisionEnabled>(&Config::collision_enabled)
        .Concept<Concept::CollisionMargin>(&Config::collision_margin)
        .Comment("How far, in centimetres, the view is held off a wall when you lean into it.\n"
                 "Keep it above the camera's near clip distance, or the wall is not drawn anyway.")
        .Concept<Concept::CollisionChannel>(&Config::collision_channel)
        .Comment("Which of the game's collision channels the wall check tests against, 0 to 31.\n"
                 "Any other number uses channel 0.")
        .Engine()
        .Concept<Concept::CollisionReleaseSmoothing>(&Config::collision_release_smoothing)
        .Concept<Concept::ToggleKey>(&Config::toggle_key)
        .Concept<Concept::CycleTrackingModeKey>(&Config::cycle_tracking_mode_key)
        .Concept<Concept::YawModeKey>(&Config::yaw_mode_key)
        .Concept<Concept::LightFollowsHead>(&Config::light_follows_head)
        .Concept<Concept::LightMultiplier>(&Config::light_multiplier)
        .Local("Aim", "AimTraceDistance", &Config::aim_trace_distance, cfg::FloatCodec(),
               "How far, in centimetres, the aim trace reaches, 100 to 1000000. The trace finds\n"
               "the point you would interact with, so the crosshair ring can sit on it.")
        .Range(kMinAimTraceDistance, kMaxAimTraceDistance)
        .Local("Aim", "AimTraceChannel", &Config::aim_trace_channel, cfg::IntCodec<int>(0, kMaxTraceChannel),
               "Which of the game's collision channels the aim trace tests against, 0 to 31.")
        .Engine()
        .Local("Dev", "InjectNextKey", &Config::inject_next_key, cfg::HotkeyCodec(),
               "For development. Steps which of the game's view point callers is given the head\n"
               "pose, to find the render path again after a game patch.")
        .Local("Dev", "InjectPreviousKey", &Config::inject_previous_key, cfg::HotkeyCodec(),
               "For development. Steps the other way.")
        .Local("Dev", "WidgetDump", &Config::widget_dump, cfg::BoolCodec(),
               "For development. true: write the game's crosshair and prompt widgets to\n"
               "HeadTracking.log, to find them again after a game patch.");
    return table;
}

cfg::RenderHeader Header() {
    cfg::RenderHeader header;
    header.display_name = kDisplayName;
    return header;
}

cfg::LegacyImport<Config> Import() {
    cfg::LegacyImport<Config> import;
    import.run = &RunImport;
    for (const legacy::Key& key : legacy::ReadKeys()) import.keys.push_back({key.section, key.key});
    return import;
}

cfg::ConfigOwnerOptions<Config> OwnerOptions(const std::wstring& exe_dir, cfg::DefaultsFile defaults) {
    cfg::ConfigOwnerOptions<Config> options;
    options.path = exe_dir + L"\\" + kIniName;
    options.table = Table();
    options.import = Import();
    options.legacy_path = exe_dir + L"\\" + kLegacyIniName;
    options.header = Header();
    options.defaults = std::move(defaults);
    return options;
}

Config Load(const std::wstring& exe_dir, cfg::DefaultsFile defaults) {
    g_owner = std::make_unique<cfg::ConfigOwner<Config>>(OwnerOptions(exe_dir, std::move(defaults)));
    const cfg::ConfigLoadResult<Config> result = g_owner->Load();
    for (const std::string& line : result.log) Log::Line("config: %s", line.c_str());
    if (!result.reason.empty()) Log::Line("config: %s", result.reason.c_str());
    Log::Line("config: %s", cfg::ConfigLoadStatusName(result.status));
    return result.config;
}

cameraunlock::TrackingMode StartupTrackingMode(const Config& config) {
    const auto mode = cameraunlock::DecodeTrackingMode(config.rotation_enabled, config.position_enabled);
    if (!mode) throw std::logic_error("RotationEnabled and PositionEnabled are both false, which the table never gives");
    return *mode;
}

void SaveWorldSpaceYaw(bool world_space_yaw) {
    Save("[General] WorldSpaceYaw", [world_space_yaw](Config& c) { c.world_space_yaw = world_space_yaw; });
}

void SaveTrackingMode(cameraunlock::TrackingMode mode) {
    const cameraunlock::TrackingModeChannels channels = cameraunlock::EncodeTrackingMode(mode);
    Save("[General] RotationEnabled and [Position] PositionEnabled", [channels](Config& c) {
        c.rotation_enabled = channels.rotation_enabled;
        c.position_enabled = channels.position_enabled;
    });
}

}  // namespace subliminal_ht::config
