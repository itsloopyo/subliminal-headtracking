// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

// The differential test for the conversion from HeadTracking.ini to the
// canonical config format.
//
// Two readings of every input:
//
//   Oracle     v0.1.0's reader and startup code, the newest published build
//              (oracle/oracle_reader.cpp)
//   Import     the frozen reader in src/legacy_config/, through the startup
//              code the commit that froze it ran it through
//
// Comparison 1, oracle against import, is what a player sees change that the
// conversion did not cause: commits since v0.1.0 that change how the file is
// read. There are none. The reader's source is the same at v0.1.0 and at the
// commit that froze it, and every core source the two compile holds the same
// code at both pins, so every field must agree bit for bit.
//
// Inputs: the file v0.1.0 writes on its first run (no release shipped or seeded
// a HeadTracking.ini), no file, an empty file, core's mutation corpus over the
// first-run file, that file with YawModeKey set to every code from 0x00 to 0xFF,
// and that file with LimitY raised and LimitYDown removed, which takes the
// reader's LimitY fallback.

#include <windows.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "legacy_config/legacy_config.h"
#include "oracle/oracle_reader.h"

#include "cameraunlock/config/legacy_import.h"
#include "cameraunlock/config/testing/ini_mutations.h"

namespace {

namespace fs = std::filesystem;
namespace cfg = cameraunlock::config;
namespace legacy = subliminal_ht::legacy;
namespace testing = cameraunlock::config::testing;

int g_failures = 0;
int g_checks = 0;

void Check(bool ok, const std::string& what) {
    ++g_checks;
    if (ok) return;
    ++g_failures;
    std::printf("FAIL: %s\n", what.c_str());
}

std::string ReadFileBytes(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("cannot open " + path.string());
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

void WriteFileBytes(const fs::path& path, const std::string& bytes) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    if (!out) throw std::runtime_error("cannot write " + path.string());
}

// ---- Scratch folders ---------------------------------------------------------
//
// One folder per reading: GetPrivateProfileString, which every reader here sits
// on, is free to cache the file it last read. `game` stands for the folder the
// game exe runs from. Every folder lives under one root for the run, removed
// once at the end.

void RemoveTree(const fs::path& root) {
    if (!fs::exists(root)) return;
    for (const auto& entry : fs::recursive_directory_iterator(root)) {
        SetFileAttributesW(entry.path().c_str(), FILE_ATTRIBUTE_NORMAL);
    }
    fs::remove_all(root);
}

const fs::path& ScratchRoot() {
    static const fs::path root = [] {
        wchar_t temp[MAX_PATH + 1] = {};
        if (GetTempPathW(MAX_PATH + 1, temp) == 0) throw std::runtime_error("GetTempPathW failed");
        fs::path r = fs::path(temp) / ("subliminal_ht_diff_" + std::to_string(GetCurrentProcessId()));
        RemoveTree(r);
        return r;
    }();
    return root;
}

class Scratch {
public:
    Scratch() {
        static unsigned s_next = 0;
        root_ = ScratchRoot() / std::to_string(s_next++);
        fs::create_directories(root_ / "game");
    }

    fs::path game() const { return root_ / "game"; }
    fs::path legacy() const { return game() / "HeadTracking.ini"; }
    // The folder as ExeDirNarrow gave it, with no trailing backslash.
    std::string exe_dir() const { return game().string(); }

    void WriteLegacy(const std::string& bytes) const { WriteFileBytes(legacy(), bytes); }

private:
    fs::path root_;
};

// ---- What a reading does -------------------------------------------------------
//
// A Record names everything the running mod acts on after reading the file:
// `field.*` the settings, `start.*` the state the session starts in, `hotkey.*`
// the bindings that can fire, each as `modifiers:code` (Ctrl 1, Shift 2, as
// cameraunlock::input::KeyModifiers numbers them) in ascending order. Floats are
// their bits.

using Record = std::map<std::string, std::string>;

std::string Bits(float value) {
    std::uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    char text[16];
    std::snprintf(text, sizeof(text), "0x%08X", static_cast<unsigned>(bits));
    return text;
}

std::string Flag(bool value) { return value ? "1" : "0"; }

const char* const kActionNames[] = {"Toggle", "CycleTrackingMode", "YawMode", "InjectNext", "InjectPrevious"};

// The bindings a set of HotkeyPoller registrations can fire. The poller skips
// code 0, and GetAsyncKeyState reports no code above 0xFF or below 0 down.
void AddHotkeys(Record& r, const std::vector<subliminal_oracle::Registration>& registrations) {
    std::map<int, std::vector<std::pair<unsigned, int>>> byAction;
    for (int action = 0; action < 5; ++action) byAction[action];
    for (const auto& [action, vk, modifiers] : registrations) {
        if (vk < 0x01 || vk > 0xFF) continue;
        byAction[action].push_back({modifiers, vk});
    }
    for (auto& [action, items] : byAction) {
        std::sort(items.begin(), items.end());
        items.erase(std::unique(items.begin(), items.end()), items.end());
        std::string text;
        for (const auto& [modifiers, vk] : items) {
            char item[32];
            std::snprintf(item, sizeof(item), "%s%u:0x%02X", text.empty() ? "" : " ", modifiers, static_cast<unsigned>(vk));
            text += item;
        }
        r[std::string("hotkey.") + kActionNames[action]] = text;
    }
}

const char* ModeName(int mode) {
    switch (mode) {
        case 0: return "RotationAndPosition";
        case 1: return "RotationOnly";
        case 2: return "PositionOnly";
        default: return "none";
    }
}

Record ObserveOracle(const subliminal_oracle::Published& g) {
    Record r;
    r["field.udp_port"] = std::to_string(g.udp_port);
    r["field.rot.yaw_sensitivity"] = Bits(g.yaw_sens);
    r["field.rot.pitch_sensitivity"] = Bits(g.pitch_sens);
    r["field.rot.roll_sensitivity"] = Bits(g.roll_sens);
    r["field.rot.invert_yaw"] = Flag(g.invert_yaw);
    r["field.rot.invert_pitch"] = Flag(g.invert_pitch);
    r["field.rot.invert_roll"] = Flag(g.invert_roll);
    r["field.local_smoothing"] = Bits(g.local_smoothing);
    r["field.remote_smoothing"] = Bits(g.remote_smoothing);
    r["field.pos.sensitivity_x"] = Bits(g.pos_sens_x);
    r["field.pos.sensitivity_y"] = Bits(g.pos_sens_y);
    r["field.pos.sensitivity_z"] = Bits(g.pos_sens_z);
    r["field.pos.limit_x"] = Bits(g.limit_x);
    r["field.pos.limit_y"] = Bits(g.limit_y);
    r["field.pos.limit_y_down"] = Bits(g.limit_y_down);
    r["field.pos.limit_z"] = Bits(g.limit_z);
    r["field.pos.limit_z_back"] = Bits(g.limit_z_back);
    r["field.collision_radius"] = Bits(g.collision_radius);
    r["field.collision_channel"] = std::to_string(g.collision_channel);
    r["field.collision_release_smoothing"] = Bits(g.collision_release_smoothing);
    r["field.aim_trace_distance"] = Bits(g.aim_trace_distance);
    r["field.aim_trace_channel"] = std::to_string(g.aim_trace_channel);
    r["field.light_multiplier"] = Bits(g.light_multiplier);
    r["start.enabled"] = Flag(g.tracking_enabled);
    r["start.mode"] = ModeName(g.tracking_mode);
    r["start.world_space_yaw"] = Flag(g.world_space_yaw);
    r["start.collision_enabled"] = Flag(g.collision_enabled);
    r["start.reticle_follows_aim"] = Flag(g.reticle_follows_aim);
    r["start.light_follows_head"] = Flag(g.light_follows_head);
    r["start.widget_dump"] = Flag(g.widget_dump);
    AddHotkeys(r, g.hotkeys);
    return r;
}

// The frozen reader's settings through the startup code of the commit that
// froze it, which is v0.1.0's: src/headtracking_mod.cpp, src/view_hook.cpp and
// src/mod_hotkeys.cpp were unchanged since the tag.
Record ObserveImport(const legacy::Config& c) {
    subliminal_oracle::Published g;
    g.udp_port = c.udp_port;
    g.tracking_enabled = c.enable_on_startup;
    g.world_space_yaw = c.world_space_yaw;
    g.yaw_sens = c.yaw_sensitivity;
    g.pitch_sens = c.pitch_sensitivity;
    g.roll_sens = c.roll_sensitivity;
    g.invert_yaw = c.invert_yaw;
    g.invert_pitch = c.invert_pitch;
    g.invert_roll = c.invert_roll;
    g.local_smoothing = c.local_smoothing;
    g.remote_smoothing = c.remote_smoothing;
    g.tracking_mode = c.position_enabled ? 0 : 1;
    g.pos_sens_x = c.position_sensitivity_x;
    g.pos_sens_y = c.position_sensitivity_y;
    g.pos_sens_z = c.position_sensitivity_z;
    g.limit_x = c.limit_x;
    g.limit_y = c.limit_y;
    g.limit_y_down = c.limit_y_down;
    g.limit_z = c.limit_z;
    g.limit_z_back = c.limit_z_back;
    g.collision_enabled = c.collision_enabled;
    g.collision_radius = c.collision_radius;
    g.collision_channel = c.collision_channel;
    g.collision_release_smoothing = c.collision_release_smoothing;
    g.reticle_follows_aim = c.reticle_follows_aim;
    g.aim_trace_distance = c.aim_trace_distance;
    g.aim_trace_channel = c.aim_trace_channel;
    g.light_follows_head = c.flashlight_follows_head;
    g.light_multiplier = c.flashlight_multiplier;
    g.widget_dump = c.widget_dump;
    g.hotkeys = {{subliminal_oracle::kToggle, 0x23, 0},
                 {subliminal_oracle::kCycleMode, 0x21, 0},
                 {subliminal_oracle::kYawMode, c.yaw_mode_key, 0},
                 {subliminal_oracle::kToggle, 0x59, 3},
                 {subliminal_oracle::kCycleMode, 0x47, 3},
                 {subliminal_oracle::kYawMode, 0x48, 3}};
    if (c.inject_hotkeys) {
        g.hotkeys.push_back({subliminal_oracle::kInjectNext, 0x55, 3});
        g.hotkeys.push_back({subliminal_oracle::kInjectPrevious, 0x4A, 3});
    }
    return ObserveOracle(g);
}

std::vector<std::string> Differences(const Record& a, const Record& b) {
    std::vector<std::string> out;
    for (const auto& [name, value] : a) {
        const auto it = b.find(name);
        if (it == b.end()) {
            out.push_back(name + " only on the left");
        } else if (it->second != value) {
            out.push_back(name + ": " + value + " / " + it->second);
        }
    }
    for (const auto& [name, value] : b) {
        if (a.find(name) == a.end()) out.push_back(name + " only on the right");
    }
    return out;
}

// ---- Inputs --------------------------------------------------------------------

fs::path DataPath(const char* name) {
    return fs::path(SUBLIMINAL_SOURCE_DIR) / "tests" / "config_differential" / "data" / name;
}

// What v0.1.0's config_write_default_if_missing wrote on the first run: the
// file every player who ran a published build holds, edited or not.
std::string FirstRun() { return ReadFileBytes(DataPath("first-run-v0.1.0.ini")); }

const char* const kFirstRunName = "v0.1.0 first-run file";

// Every key the frozen reader reads, and how the corpus varies each one. The
// reader refuses a port outside 1024-65535, a trace channel outside 0-31 and a
// yaw key that is not bindable or is End or Page Up; it clamps every float it
// reads into its range and replaces a float that is not finite, or does not
// parse whole, with the key's fallback.
std::vector<testing::MutationKey> CorpusKeys() {
    return {
        {"Network", "UdpPort", "5771", {"80", "70000"}},
        {"General", "EnableOnStartup", "0", {}},
        {"General", "WorldSpaceYaw", "0", {}},
        {"Rotation", "YawSensitivity", "0.5", {"-150", "150"}},
        {"Rotation", "PitchSensitivity", "0.5", {"-150", "150"}},
        {"Rotation", "RollSensitivity", "0.5", {"-150", "150"}},
        {"Rotation", "InvertYaw", "1", {}},
        {"Rotation", "InvertPitch", "1", {}},
        {"Rotation", "InvertRoll", "1", {}},
        {"Rotation", "LocalSmoothing", "0.3", {"-0.5", "1.5"}},
        {"Rotation", "RemoteSmoothing", "0.6", {"-0.5", "1.5"}},
        {"Position", "Enabled", "0", {}},
        {"Position", "SensitivityX", "0.5", {"-150", "150"}},
        {"Position", "SensitivityY", "0.5", {"-150", "150"}},
        {"Position", "SensitivityZ", "0.5", {"-150", "150"}},
        {"Position", "LimitX", "0.5", {"-0.3", "12"}},
        {"Position", "LimitY", "0.35", {"-0.3", "12"}},
        {"Position", "LimitYDown", "0.15", {"-0.3", "12"}},
        {"Position", "LimitZ", "0.6", {"-0.3", "12"}},
        {"Position", "LimitZBack", "0.15", {"-0.3", "12"}},
        {"Collision", "CollisionEnabled", "0", {}},
        {"Collision", "CollisionRadius", "35", {"5", "150"}},
        {"Collision", "CollisionChannel", "2", {"-1", "32"}},
        {"Collision", "CollisionReleaseSmoothing", "0.5", {"-0.5", "1.5"}},
        {"Reticle", "Enabled", "0", {}},
        {"Reticle", "TraceDistance", "5000", {"50", "2000000"}},
        {"Reticle", "TraceChannel", "3", {"-1", "32"}},
        {"Flashlight", "Enabled", "0", {}},
        {"Flashlight", "Multiplier", "1.0", {"-1", "7.5"}},
        {"Hotkeys", "YawModeKey", "0x2E", {"0x100"}, true},
        {"Dev", "InjectHotkeys", "1", {}},
        {"Dev", "WidgetDump", "1", {}},
    };
}

std::vector<cfg::LegacyKey> CorpusReads() {
    std::vector<cfg::LegacyKey> keys;
    for (const legacy::Key& key : legacy::ReadKeys()) keys.push_back({key.section, key.key});
    return keys;
}

struct Input {
    std::string name;
    bool present;
    std::string bytes;
};

std::string Replace(std::string text, const std::string& from, const std::string& to) {
    const std::size_t at = text.find(from);
    if (at == std::string::npos) throw std::logic_error("'" + from + "' is not in the text");
    return text.replace(at, from.size(), to);
}

std::vector<Input> Inputs() {
    std::vector<Input> inputs = {
        {kFirstRunName, true, FirstRun()},
        {"no file", false, {}},
        {"empty file", true, {}},
        {"LimitY=0.35 without LimitYDown", true,
         Replace(Replace(FirstRun(), "LimitY=0.20\r\n", "LimitY=0.35\r\n"), "LimitYDown=0.20\r\n", "")},
    };
    for (testing::IniMutation& m : testing::GenerateIniMutations(FirstRun(), CorpusReads(), CorpusKeys())) {
        inputs.push_back({"corpus: " + m.name, true, std::move(m.bytes)});
    }
    for (int code = 0x00; code <= 0xFF; ++code) {
        char text[32];
        std::snprintf(text, sizeof(text), "YawModeKey=0x%02X", static_cast<unsigned>(code));
        inputs.push_back({text, true, Replace(FirstRun(), "YawModeKey=0x22", text)});
    }
    return inputs;
}

// ---- Comparison 1 ----------------------------------------------------------------

void Compare(const std::vector<Input>& inputs) {
    int compared = 0;
    for (const Input& input : inputs) {
        const std::string& name = input.name;
        Scratch s;
        if (input.present) s.WriteLegacy(input.bytes);
        const Record oracle = ObserveOracle(subliminal_oracle::Read(s.exe_dir()));

        // v0.1.0 writes its default file where none exists, and nothing else.
        if (!input.present) {
            Check(ReadFileBytes(s.legacy()) == FirstRun(), name + ": v0.1.0's first run writes the committed first-run file");
        } else {
            Check(ReadFileBytes(s.legacy()) == input.bytes, name + ": v0.1.0 leaves an existing file as it was");
        }

        // The frozen reader, on its own copy of the input, so it finds no
        // file where the player had none.
        Scratch t;
        if (input.present) t.WriteLegacy(input.bytes);
        legacy::Config read;
        const bool present = legacy::Load(t.legacy().string(), read);
        Check(present == input.present, name + ": the frozen reader finds the file exactly when it is there");
        Check(input.present || !fs::exists(t.legacy()), name + ": the frozen reader writes no file");
        const Record imported = ObserveImport(read);

        const std::vector<std::string> diff = Differences(oracle, imported);
        for (const std::string& d : diff) std::printf("  comparison 1, %s: %s\n", name.c_str(), d.c_str());
        Check(diff.empty(), name + ": comparison 1, the oracle and the import agree");
        ++compared;
    }
    std::printf("comparison 1: %d inputs\n", compared);
}

}  // namespace

int main() {
    // Unbuffered, so the lines before an exception reach the log.
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    try {
        const std::vector<Input> inputs = Inputs();
        Compare(inputs);
        RemoveTree(ScratchRoot());
    } catch (const std::exception& e) {
        std::printf("FAIL: %s\n", e.what());
        return 1;
    }
    std::printf("%d checks, %d failed\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
