// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

// CameraUnlock.ini in the canonical config format.
//
// The committed CameraUnlock.ini is the table's fresh render, which is also what
// the owner creates beside the game exe at first launch: `default` on every
// global row, so each follows Defaults.ini, the game's own collision margin and
// channel, and the mod's own rows at their defaults. A toggle's save changes the
// lines of its rows and no other byte. An older HeadTracking.ini is imported
// once into a new CameraUnlock.ini through the frozen import and is never
// written; tests/config_differential/ holds that to the published build over
// the whole corpus, and the cases here are the ones worth reading as examples.
//
// `subliminal_config_tests --render-config <path>` writes the fresh render to
// <path> and exits, which is how `pixi run render-config` rewrites the committed
// file after a change to a row, a comment or a default.

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

#include <windows.h>

#include "config.h"

namespace {

namespace cfg = ::cameraunlock::config;
namespace fs = std::filesystem;
using cameraunlock::TrackingMode;

int g_checks = 0;
int g_failures = 0;

void Check(bool ok, const std::string& what) {
    ++g_checks;
    if (ok) return;
    ++g_failures;
    std::printf("FAIL %s\n", what.c_str());
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

std::string Rendered() {
    return cfg::RenderCanonicalFresh(subliminal_ht::config::Table(), subliminal_ht::config::Header());
}

std::string CommittedFile() { return ReadFileBytes(fs::path(SUBLIMINAL_SOURCE_DIR) / "CameraUnlock.ini"); }

// A folder of its own per case, removed afterwards: `game` stands for the folder
// holding the game exe, and Defaults.ini sits in `global` beside it.
class Scratch {
public:
    explicit Scratch(const char* tag) {
        wchar_t temp[MAX_PATH + 1] = {};
        if (GetTempPathW(MAX_PATH + 1, temp) == 0) throw std::runtime_error("GetTempPathW failed");
        root_ = fs::path(temp) /
                ("subliminal_ht_config_" + std::string(tag) + "_" + std::to_string(GetCurrentProcessId()));
        fs::remove_all(root_);
        fs::create_directories(game());
    }
    Scratch(const Scratch&) = delete;
    Scratch& operator=(const Scratch&) = delete;
    // A scanner can still hold a file the test just wrote, and a destructor must
    // not throw, so a folder left behind is reported and the run carries on.
    ~Scratch() {
        std::error_code error;
        fs::remove_all(root_, error);
        if (error) std::printf("  scratch folder left behind: %s: %s\n", root_.string().c_str(), error.message().c_str());
    }

    fs::path game() const { return root_ / "game"; }
    fs::path ini() const { return game() / "CameraUnlock.ini"; }
    fs::path legacy() const { return game() / "HeadTracking.ini"; }
    fs::path defaults() const { return root_ / "global" / "Defaults.ini"; }

    subliminal_ht::Config Load() const {
        return subliminal_ht::config::Load(game().wstring(), cfg::DefaultsFile::At(defaults().wstring()));
    }

    std::set<std::string> Names() const {
        std::set<std::string> names;
        for (const auto& entry : fs::directory_iterator(game())) names.insert(entry.path().filename().string());
        return names;
    }

private:
    fs::path root_;
};

std::vector<std::string> Lines(const std::string& bytes) {
    std::vector<std::string> lines;
    std::size_t start = 0;
    for (std::size_t end; (end = bytes.find("\r\n", start)) != std::string::npos; start = end + 2) {
        lines.push_back(bytes.substr(start, end - start));
    }
    return lines;
}

// The lines that differ between two files of the same line count, or "count" when
// the counts differ.
std::vector<std::string> ChangedLines(const std::string& before, const std::string& after) {
    const std::vector<std::string> a = Lines(before), b = Lines(after);
    if (a.size() != b.size()) return {"count"};
    std::vector<std::string> changed;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (a[i] != b[i]) changed.push_back(b[i]);
    }
    return changed;
}

bool Holds(const std::string& bytes, const std::string& line) {
    return bytes.find("\r\n" + line + "\r\n") != std::string::npos;
}

void TheCommittedFileIsTheFreshRender() {
    Check(Rendered() == CommittedFile(), "CameraUnlock.ini is the table's fresh render; run pixi run render-config");
}

// Every global row holds `default`; the collision margin and channel are the
// game's own, and the aim trace and dev rows are this mod's.
void TheCommittedFileFollowsDefaultsIni() {
    const std::string committed = CommittedFile();
    for (const char* line :
         {"UdpPort=default", "EnableOnStartup=default", "WorldSpaceYaw=default", "RotationEnabled=default",
          "LocalSmoothing=default", "RemoteSmoothing=default", "PositionEnabled=default",
          "PositionLimitX=default", "PositionLimitY=default", "PositionLimitYDown=default", "PositionLimitZ=default",
          "PositionLimitZBack=default", "CollisionEnabled=default", "CollisionMargin=20.0", "; CollisionChannel=0",
          "CollisionReleaseSmoothing=default", "ToggleKey=default", "CycleTrackingModeKey=default",
          "YawModeKey=default", "LightFollowsHead=default", "LightMultiplier=default", "AimTraceDistance=20000.0",
          "; AimTraceChannel=0", "InjectNextKey=", "InjectPreviousKey=", "WidgetDump=false"}) {
        Check(Holds(committed, line), std::string("the committed file holds ") + line);
    }
    Check(committed.find("Sensitivity") == std::string::npos && committed.find("Invert") == std::string::npos,
          "the committed file has no sensitivity or inversion");
    Check(committed.find("[Reticle]") == std::string::npos, "the committed file has no reticle setting");
}

void FirstLaunchCreatesTheCommittedFile() {
    Scratch s("created");
    const subliminal_ht::Config loaded = s.Load();
    Check(ReadFileBytes(s.ini()) == CommittedFile(), "the first launch writes the committed file byte for byte");
    Check(s.Names() == std::set<std::string>{"CameraUnlock.ini"},
          "the first launch creates CameraUnlock.ini and nothing else beside the exe");
    Check(fs::exists(s.defaults()), "the first launch creates Defaults.ini where none exists");
    Check(loaded.toggle_key == "End, Ctrl+Shift+Y", "ToggleKey starts at End, Ctrl+Shift+Y");
    Check(loaded.cycle_tracking_mode_key == "PageUp, Ctrl+Shift+G", "CycleTrackingModeKey starts at PageUp, Ctrl+Shift+G");
    Check(loaded.yaw_mode_key == "PageDown, Ctrl+Shift+H", "YawModeKey starts at PageDown, Ctrl+Shift+H");
    Check(loaded.inject_next_key.empty() && loaded.inject_previous_key.empty(), "the dev inject keys start unbound");
    Check(loaded.enable_on_startup && loaded.world_space_yaw, "tracking starts on, in world-space yaw");
    Check(subliminal_ht::config::StartupTrackingMode(loaded) == TrackingMode::RotationAndPosition,
          "tracking starts in rotation and position");
    Check(loaded.collision_enabled && loaded.collision_margin == 20.0f && loaded.collision_channel == 0,
          "the wall check starts on, 20cm off a wall, on channel 0");
    Check(loaded.light_follows_head && loaded.light_multiplier == 1.5f, "the flashlight follows the head at 1.5");
    Check(loaded.aim_trace_distance == 20000.0f && loaded.aim_trace_channel == 0,
          "the aim trace reaches 200m on channel 0");
}

// A value in Defaults.ini reaches every row holding `default`, and never the
// game's own collision margin.
void ADefaultRowFollowsDefaultsIni() {
    Scratch s("follows");
    s.Load();
    WriteFileBytes(s.defaults(), "[CameraUnlock]\r\nConfigFormat=1\r\n\r\n[General]\r\nWorldSpaceYaw=false\r\n\r\n"
                                 "[Position]\r\nCollisionEnabled=false\r\nCollisionMargin=5.0\r\n\r\n"
                                 "[Hotkeys]\r\nToggleKey=F8\r\n\r\n[Light]\r\nLightMultiplier=1.0\r\n");
    const subliminal_ht::Config c = s.Load();
    Check(c.toggle_key == "F8", "ToggleKey follows Defaults.ini");
    Check(!c.world_space_yaw, "WorldSpaceYaw follows Defaults.ini");
    Check(!c.collision_enabled, "CollisionEnabled follows Defaults.ini");
    Check(c.light_multiplier == 1.0f, "LightMultiplier follows Defaults.ini");
    Check(c.collision_margin == 20.0f, "CollisionMargin stays the game's own");
}

void TheYawToggleSavesItsLineAndNothingElse() {
    Scratch s("save_yaw");
    s.Load();
    const std::string before = ReadFileBytes(s.ini());
    const std::string defaults = ReadFileBytes(s.defaults());
    subliminal_ht::config::SaveWorldSpaceYaw(false);
    Check(ChangedLines(before, ReadFileBytes(s.ini())) == std::vector<std::string>{"WorldSpaceYaw=false"},
          "a yaw save writes WorldSpaceYaw over default, and nothing else");
    Check(ReadFileBytes(s.defaults()) == defaults, "a save leaves Defaults.ini as it was");
    Check(!s.Load().world_space_yaw, "the saved yaw mode comes back at the next launch");
}

// The mode is one setting in two rows, so a save writes both.
void TheModeCycleSavesThePair() {
    Scratch s("save_mode");
    s.Load();
    const std::string before = ReadFileBytes(s.ini());

    subliminal_ht::config::SaveTrackingMode(TrackingMode::RotationOnly);
    Check(ChangedLines(before, ReadFileBytes(s.ini())) ==
              (std::vector<std::string>{"RotationEnabled=true", "PositionEnabled=false"}),
          "rotation only writes the pair over default");

    subliminal_ht::config::SaveTrackingMode(TrackingMode::PositionOnly);
    Check(ChangedLines(before, ReadFileBytes(s.ini())) ==
              (std::vector<std::string>{"RotationEnabled=false", "PositionEnabled=true"}),
          "position only writes the pair");
    Check(subliminal_ht::config::StartupTrackingMode(s.Load()) == TrackingMode::PositionOnly,
          "the saved mode comes back at the next launch");

    subliminal_ht::config::SaveTrackingMode(TrackingMode::RotationAndPosition);
    Check(ChangedLines(before, ReadFileBytes(s.ini())) ==
              (std::vector<std::string>{"RotationEnabled=true", "PositionEnabled=true"}),
          "back to full, the pair holds values");
}

// The legacy file is imported into a new CameraUnlock.ini and left as it was.
void TheLegacyFileIsImportedAndLeftAsItWas() {
    Scratch s("import");
    const std::string legacy =
        "[General]\r\nWorldSpaceYaw=0\r\n; my note\r\n[Rotation]\r\nYawSensitivity=1.5\r\nRemoteSmoothing=0.40\r\n"
        "[Position]\r\nEnabled=0\r\nLimitY=0.35\r\n[Collision]\r\nCollisionRadius=35\r\nCollisionChannel=2\r\n"
        "[Reticle]\r\nEnabled=0\r\nTraceDistance=5000\r\n[Flashlight]\r\nMultiplier=1.0\r\n"
        "[Hotkeys]\r\nYawModeKey=0x2E\r\n[Dev]\r\nInjectHotkeys=1\r\n";
    WriteFileBytes(s.legacy(), legacy);
    const subliminal_ht::Config c = s.Load();
    Check(!c.world_space_yaw, "WorldSpaceYaw=0 is carried");
    Check(c.remote_smoothing == 0.4f, "RemoteSmoothing is carried");
    Check(subliminal_ht::config::StartupTrackingMode(c) == TrackingMode::RotationOnly,
          "[Position] Enabled=0 starts in rotation only");
    Check(c.position_limit_y == 0.35f && c.position_limit_y_down == 0.35f,
          "LimitY without LimitYDown bounds both vertical directions");
    Check(c.collision_margin == 35.0f && c.collision_channel == 2, "the collision radius and channel are carried");
    Check(c.aim_trace_distance == 5000.0f, "the aim trace distance is carried");
    Check(c.light_multiplier == 1.0f, "the flashlight multiplier is carried");
    Check(c.yaw_mode_key == "Delete, Ctrl+Shift+H", "the yaw key is carried beside its chord");
    Check(c.inject_next_key == "Ctrl+Shift+U" && c.inject_previous_key == "Ctrl+Shift+J",
          "InjectHotkeys=1 binds the chords it bound");
    Check(ReadFileBytes(s.legacy()) == legacy, "HeadTracking.ini keeps its bytes");
    Check((s.Names() == std::set<std::string>{"CameraUnlock.ini", "HeadTracking.ini"}),
          "the import creates CameraUnlock.ini and nothing else");
    const std::string migrated = ReadFileBytes(s.ini());
    for (const char* line :
         {"WorldSpaceYaw=false", "RemoteSmoothing=0.4", "RotationEnabled=true", "PositionEnabled=false",
          "PositionLimitY=0.35", "PositionLimitYDown=0.35", "CollisionMargin=35.0", "CollisionChannel=2",
          "AimTraceDistance=5000.0", "LightMultiplier=1.0", "YawModeKey=Delete, Ctrl+Shift+H",
          "InjectNextKey=Ctrl+Shift+U", "InjectPreviousKey=Ctrl+Shift+J", "LocalSmoothing=default",
          "ToggleKey=default", "CollisionEnabled=default"}) {
        Check(Holds(migrated, line), std::string("the migrated file holds ") + line);
    }
    Check(migrated.find("Sensitivity") == std::string::npos, "a changed sensitivity is not carried");
    Check(migrated.find("[Reticle]") == std::string::npos, "[Reticle] Enabled=0 is not carried");

    // Once CameraUnlock.ini exists, HeadTracking.ini is not read again.
    WriteFileBytes(s.legacy(), "[General]\r\nWorldSpaceYaw=1\r\n");
    Check(!s.Load().world_space_yaw, "the next launch reads CameraUnlock.ini, not HeadTracking.ini");
    Check(ReadFileBytes(s.ini()) == migrated, "the next launch writes nothing");
}

}  // namespace

int main(int argc, char** argv) {
    if (argc == 3 && std::strcmp(argv[1], "--render-config") == 0) {
        WriteFileBytes(argv[2], Rendered());
        return 0;
    }

    TheCommittedFileIsTheFreshRender();
    TheCommittedFileFollowsDefaultsIni();
    FirstLaunchCreatesTheCommittedFile();
    ADefaultRowFollowsDefaultsIni();
    TheYawToggleSavesItsLineAndNothingElse();
    TheModeCycleSavesThePair();
    TheLegacyFileIsImportedAndLeftAsItWas();

    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
