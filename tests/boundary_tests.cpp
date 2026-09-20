// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

// Behaviour locks for the two pieces of the mod that are pure functions of
// their arguments: the tracker-to-Unreal camera boundary, and the
// GetPlayerViewPoint caller gate.
//
// The numbers here are not derived from first principles - they are the numbers
// the shipped mod produced before the code was reorganised, recorded so that a
// later change to signs, axis mapping or composition order cannot pass quietly.
// A failure means the view or the aim decoupling has moved.

#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "camera_boundary.h"
#include "cameraunlock/effects/head_follow_light.h"
#include "inject_mode.h"

namespace {

namespace ue = ::cameraunlock::unreal;
using subliminal_ht::camera_boundary::ApplyHeadPose;
using subliminal_ht::camera_boundary::PositionOffset;

int g_failures = 0;

void Check(bool ok, const char* what) {
    if (ok) return;
    std::printf("FAIL: %s\n", what);
    ++g_failures;
}

// The position offsets come in as floats, so their exact decimal value is
// already a few parts in 10^7 off before any arithmetic happens; those checks
// pass the looser tolerance. The rotations are doubles throughout.
constexpr double kAngleTolerance = 1e-6;
constexpr double kOffsetToleranceCm = 1e-4;

void CheckNear(double actual, double expected, const char* what,
               double tolerance = kAngleTolerance) {
    if (std::fabs(actual - expected) <= tolerance) return;
    std::printf("FAIL: %s (expected %.9f, got %.9f)\n", what, expected, actual);
    ++g_failures;
}

// ---- camera boundary: rotation -------------------------------------------

void WorldYawAddsPoseAndNegatesRoll() {
    ue::FRotator r{10.0, 20.0, 30.0};
    ApplyHeadPose(r, 5.0, 3.0, 7.0, /*worldSpaceYaw=*/true);
    CheckNear(r.Pitch, 13.0, "world yaw: pitch adds");
    CheckNear(r.Yaw,   25.0, "world yaw: yaw adds");
    CheckNear(r.Roll,  23.0, "world yaw: roll subtracts");
}

void ZeroPoseIsIdentityInBothModes() {
    ue::FRotator world{12.0, -34.0, 5.0};
    ApplyHeadPose(world, 0.0, 0.0, 0.0, true);
    CheckNear(world.Pitch, 12.0, "world yaw: zero pose leaves pitch");
    CheckNear(world.Yaw,  -34.0, "world yaw: zero pose leaves yaw");
    CheckNear(world.Roll,   5.0, "world yaw: zero pose leaves roll");

    ue::FRotator local{12.0, -34.0, 5.0};
    ApplyHeadPose(local, 0.0, 0.0, 0.0, false);
    CheckNear(local.Pitch, 12.0, "local yaw: zero pose leaves pitch");
    CheckNear(local.Yaw,  -34.0, "local yaw: zero pose leaves yaw");
    CheckNear(local.Roll,   5.0, "local yaw: zero pose leaves roll");
}

// From a level camera the two modes agree: camera-local up is world up, so the
// quaternion compose and the FRotator addition describe the same rotation. They
// only part company once the base rotation is pitched, which is the whole reason
// the yaw-mode toggle exists.
void ModesAgreeOnLevelBaseAndDivergeWhenPitched() {
    ue::FRotator world{0.0, 40.0, 0.0};
    ue::FRotator local = world;
    ApplyHeadPose(world, 15.0, 10.0, 0.0, true);
    ApplyHeadPose(local, 15.0, 10.0, 0.0, false);
    CheckNear(local.Pitch, world.Pitch, "level base: pitch agrees across modes");
    CheckNear(local.Yaw,   world.Yaw,   "level base: yaw agrees across modes");
    CheckNear(local.Roll,  world.Roll,  "level base: roll agrees across modes");

    ue::FRotator pitchedWorld{-50.0, 40.0, 0.0};
    ue::FRotator pitchedLocal = pitchedWorld;
    ApplyHeadPose(pitchedWorld, 30.0, 0.0, 0.0, true);
    ApplyHeadPose(pitchedLocal, 30.0, 0.0, 0.0, false);
    Check(std::fabs(pitchedLocal.Roll - pitchedWorld.Roll) > 1.0,
          "pitched base: camera-local yaw leans the horizon, world yaw does not");
    CheckNear(pitchedWorld.Roll, 0.0, "pitched base: world yaw keeps the horizon level");
}

// The composition the shipped mod renders with, recorded angle for angle.
void LocalYawCompositionIsUnchanged() {
    ue::FRotator r{-20.0, 35.0, 4.0};
    ApplyHeadPose(r, 12.0, 8.0, 6.0, false);
    CheckNear(r.Pitch, -12.375926322, "local yaw: pitch");
    CheckNear(r.Yaw,    47.721496771, "local yaw: yaw");
    CheckNear(r.Roll,   -6.410190882, "local yaw: roll");
}

// A view pitch past a quarter turn has gone over the top: the horizon inverts
// while the head is still tilting one way. The game lets the player look to
// within a tenth of a degree of straight down, so world-yaw mode - which adds
// the head pitch to the base directly - reaches it with an ordinary downward
// tilt. The camera-local branch needs no clamp: QuatToRotator returns pitch out
// of asin, so it is inside the range for every finite quaternion.
void WorldYawHoldsThePitchInsideTheEnginesRange() {
    // A non-zero head yaw, so the yaw assertion distinguishes "the pitch clamp
    // leaves yaw alone" from "yaw is ignored".
    ue::FRotator world{-89.9, 40.0, 0.0};
    ApplyHeadPose(world, 25.0, -20.0, 0.0, true);
    Check(world.Pitch >= -90.0 && world.Pitch <= 90.0,
          "world yaw: a downward tilt at the pitch limit stays inside +/-90");
    CheckNear(world.Pitch, -89.9, "world yaw: pitch holds at the engine's own limit");
    CheckNear(world.Yaw,    65.0, "world yaw: the pitch clamp does not disturb yaw");

    ue::FRotator up{89.9, 40.0, 0.0};
    ApplyHeadPose(up, 0.0, 20.0, 0.0, true);
    CheckNear(up.Pitch, 89.9, "world yaw: the upward limit holds too");

}

// ---- camera boundary: position -------------------------------------------

// With the camera at the world origin rotation, the three tracker axes land on
// the engine axes the mod's sign notes describe: -z is the forward lean, x is
// mirrored, y is up. Metres in, centimetres out.
void PositionOffsetMapsTrackerAxesToUnreal() {
    const ue::FQuat4d identity{0.0, 0.0, 0.0, 1.0};

    const ue::FVector surge = PositionOffset(identity, 0.0f, 0.0f, -0.40f);
    CheckNear(surge.X, 40.0, "surge: negative tracker z leans forward, in cm", kOffsetToleranceCm);
    CheckNear(surge.Y,  0.0, "surge: no sway", kOffsetToleranceCm);
    CheckNear(surge.Z,  0.0, "surge: no heave", kOffsetToleranceCm);

    const ue::FVector sway = PositionOffset(identity, 0.30f, 0.0f, 0.0f);
    CheckNear(sway.X,   0.0, "sway: no surge", kOffsetToleranceCm);
    CheckNear(sway.Y, -30.0, "sway: tracker x is mirrored against engine right", kOffsetToleranceCm);
    CheckNear(sway.Z,   0.0, "sway: no heave", kOffsetToleranceCm);

    const ue::FVector heave = PositionOffset(identity, 0.0f, 0.20f, 0.0f);
    CheckNear(heave.X,  0.0, "heave: no surge", kOffsetToleranceCm);
    CheckNear(heave.Y,  0.0, "heave: no sway", kOffsetToleranceCm);
    CheckNear(heave.Z, 20.0, "heave: tracker y maps straight to engine up", kOffsetToleranceCm);
}

// The offset is built in the CLEAN camera frame, so it follows the body: yaw the
// camera 90 degrees and a forward lean has to come out along engine +Y.
void PositionOffsetFollowsTheCameraBasis() {
    const ue::FQuat4d yawed = ue::QuatFromEulerDeg(0.0, 90.0, 0.0);
    const ue::FVector surge = PositionOffset(yawed, 0.0f, 0.0f, -0.40f);
    CheckNear(surge.X,  0.0, "yawed surge: nothing left on engine X", kOffsetToleranceCm);
    CheckNear(surge.Y, 40.0, "yawed surge: forward lean follows the camera", kOffsetToleranceCm);
    CheckNear(surge.Z,  0.0, "yawed surge: nothing on engine Z", kOffsetToleranceCm);
}

// ---- caller gate ----------------------------------------------------------

void InjectGateSelectsOnlyItsOwnCaller() {
    subliminal_ht::inject::CallerRvas callers{};
    callers[0] = 0x038d91ec;  // the render caller
    callers[1] = 0x03b1047f;

    using namespace subliminal_ht::inject;
    Check(ShouldInject(0x038d91ec, kFirstCaller, callers), "gate: render caller injects");
    Check(!ShouldInject(0x03b1047f, kFirstCaller, callers), "gate: other callers stay clean");
    Check(ShouldInject(0x03b1047f, kFirstCaller + 1, callers), "gate: mode 2 selects caller 2");

    Check(ShouldInject(0x0, kAllCallers, callers), "gate: all-callers mode injects everywhere");
    Check(!ShouldInject(0x038d91ec, kNone, callers), "gate: none mode injects nowhere");

    // An empty slot must never match, or a profile with fewer callers than slots
    // would inject for every caller whose return RVA failed to resolve.
    Check(!ShouldInject(0x0, kFirstCaller + 5, callers), "gate: empty slot never matches");
}

void InjectCycleWrapsThroughEveryMode() {
    using namespace subliminal_ht::inject;
    Check(Cycle(kAllCallers, +1) == kFirstCaller, "cycle: forward off all-callers");
    Check(Cycle(kNone, +1) == kAllCallers, "cycle: forward wraps past none");
    Check(Cycle(kAllCallers, -1) == kNone, "cycle: backward wraps to none");
    Check(kModeCount == static_cast<int>(kCallerSlots) + 2, "cycle: mode count covers every slot");

    int mode = kAllCallers;
    for (int i = 0; i < kModeCount; ++i) mode = Cycle(mode, +1);
    Check(mode == kAllCallers, "cycle: a full lap returns to where it started");
}

void InjectCallerRvaOnlyResolvesForSingleCallerModes() {
    subliminal_ht::inject::CallerRvas callers{};
    callers[0] = 0x038d91ec;
    using namespace subliminal_ht::inject;
    Check(CallerRva(kFirstCaller, callers) == 0x038d91ec, "rva: single-caller mode resolves");
    Check(CallerRva(kAllCallers, callers) == 0, "rva: all-callers mode has no single RVA");
    Check(CallerRva(kNone, callers) == 0, "rva: none mode has no RVA");
}

// The flashlight leads the head by a multiplier, and the beam is composed by
// the SAME function the camera goes through. These lock that the two cannot
// drift apart: at a multiplier of 1 the beam must land exactly on the view, and
// at 0 exactly on the clean aim, in BOTH yaw modes.
void FlashlightBeamComposesLikeTheCamera() {
    using cameraunlock::effects::ScaleHeadEuler;
    using cameraunlock::unreal::FRotator;

    auto compose = [](FRotator base, float yaw, float pitch, float roll,
                      float multiplier, bool worldYaw) {
        const auto s = ScaleHeadEuler({yaw, pitch, roll}, multiplier);
        subliminal_ht::camera_boundary::ApplyHeadPose(base, s.yaw, s.pitch, s.roll,
                                                      worldYaw);
        return base;
    };
    auto same = [](const FRotator& a, const FRotator& b) {
        return std::fabs(a.Pitch - b.Pitch) < 1e-9 &&
               std::fabs(a.Yaw   - b.Yaw)   < 1e-9 &&
               std::fabs(a.Roll  - b.Roll)  < 1e-9;
    };

    const FRotator base{-12.0, 47.0, 0.0};
    for (bool worldYaw : {true, false}) {
        FRotator view = base;
        subliminal_ht::camera_boundary::ApplyHeadPose(view, 18.0, 9.0, 6.0, worldYaw);
        Check(same(compose(base, 18.0f, 9.0f, 6.0f, 1.0f, worldYaw), view),
              "a multiplier of 1 lands the beam exactly on the view");
        Check(same(compose(base, 18.0f, 9.0f, 6.0f, 0.0f, worldYaw), base),
              "a multiplier of 0 pins the beam to the clean aim");
    }
}

// The scaling is per-axis, because that is how this mod composes the pose onto
// the camera. Core ships the other spelling too and warns they disagree, so the
// choice is pinned here rather than left to whichever looked tidier.
void FlashlightMultiplierScalesEachAxis() {
    const auto s = cameraunlock::effects::ScaleHeadEuler({10.0f, -4.0f, 6.0f}, 1.5f);
    Check(std::fabs(s.yaw   - 15.0f) < 1e-5f, "yaw scales by the multiplier");
    Check(std::fabs(s.pitch + 6.0f)  < 1e-5f, "pitch scales by the multiplier");
    Check(std::fabs(s.roll  - 9.0f)  < 1e-5f, "roll scales with the rest");

    // The fleet default, asserted as a number: the beam LEADS the view, and a
    // change to it is a change every mod feels.
    Check(std::fabs(cameraunlock::effects::kDefaultLightMultiplier - 1.5f) < 1e-6f,
          "the shared light multiplier default is 1.5");
}

}  // namespace

int main() {
    WorldYawAddsPoseAndNegatesRoll();
    ZeroPoseIsIdentityInBothModes();
    ModesAgreeOnLevelBaseAndDivergeWhenPitched();
    LocalYawCompositionIsUnchanged();
    WorldYawHoldsThePitchInsideTheEnginesRange();
    PositionOffsetMapsTrackerAxesToUnreal();
    PositionOffsetFollowsTheCameraBasis();
    InjectGateSelectsOnlyItsOwnCaller();
    InjectCycleWrapsThroughEveryMode();
    InjectCallerRvaOnlyResolvesForSingleCallerModes();
    FlashlightBeamComposesLikeTheCamera();
    FlashlightMultiplierScalesEachAxis();

    if (g_failures != 0) {
        std::printf("%d check(s) failed\n", g_failures);
        return EXIT_FAILURE;
    }
    std::printf("all checks passed\n");
    return EXIT_SUCCESS;
}
