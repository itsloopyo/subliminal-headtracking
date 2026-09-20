// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

// Behaviour locks for the reticle's arithmetic: the basis-to-basis projection
// and the field-of-view axis model. Both are header-only on the mod side, so
// these run with no engine and no game.
//
// The two cases that matter most are the release gates from the reticle
// doctrine, and they are encoded here rather than left to a play session:
//
//   * a lean with the rotation centred moves the mark by lean/distance, so the
//     SAME lean against a surface at 1m and at 10m produces DIFFERENT offsets.
//     A projection that used a fixed, smoothed or stale depth would produce the
//     same number twice, which is the fault that reads as "correct at one range
//     and wrong either side of it".
//   * opposite leans of equal size produce equal and opposite offsets, because
//     the shot itself has not moved.

#include <cmath>
#include <cstdio>
#include <initializer_list>

#include "aim_projection.h"
#include "camera_fov.h"

namespace {

namespace ue = ::cameraunlock::unreal;
using subliminal_ht::aim_projection::ProjectFromRenderedEye;
namespace fov = subliminal_ht::camera_fov;

int g_failures = 0;

void Check(bool ok, const char* what) {
    if (ok) return;
    std::printf("FAIL: %s\n", what);
    ++g_failures;
}

void CheckNear(double actual, double expected, const char* what, double tol = 1e-5) {
    if (std::fabs(actual - expected) <= tol) return;
    std::printf("FAIL: %s (got %.9f, expected %.9f)\n", what, actual, expected);
    ++g_failures;
}

// Identity rotation: UE's camera basis is X forward, Y right, Z up.
constexpr ue::FQuat4d kNoRotation{0.0, 0.0, 0.0, 1.0};

// A 90 degree horizontal field, so tan(fov/2) is exactly 1 and the NDC value IS
// the tangent of the angle off axis. That keeps every expected number below
// readable as geometry rather than as a fixture.
constexpr float kTan = 1.0f;

// The vector the hook hands the projection: from the eye the frame is drawn
// from to the point the ray stopped on.
ue::FVector Aim(const ue::FVector& renderEye, const ue::FVector& point) {
    return ue::FVector{point.X - renderEye.X, point.Y - renderEye.Y, point.Z - renderEye.Z};
}

void TestCentredHeadMarksTheCentre() {
    // No lean, no head rotation: the ray leaves the eye the frame is drawn
    // from, so the mark is dead centre whatever the surface is at.
    for (double d : {50.0, 200.0, 5000.0}) {
        const ue::FVector eye{0.0, 0.0, 0.0};
        const ue::FVector point{d, 0.0, 0.0};
        float x = -1.0f, y = -1.0f;
        Check(ProjectFromRenderedEye(kNoRotation, Aim(eye, point), kTan, kTan, x, y),
              "centred head projects");
        CheckNear(x, 0.0, "centred head: ndc x is centre");
        CheckNear(y, 0.0, "centred head: ndc y is centre");
    }
}

void TestLeanParallaxScalesWithDistance() {
    // 30cm to the player's right, rotation centred, against a surface 1m away
    // and then 10m away. UE +Y is right, so the mark moves LEFT as the eye
    // moves right - that is the parallax, and it is ten times larger up close.
    const double lean = 30.0;
    const ue::FVector eye{0.0, lean, 0.0};

    float nearX = 0.0f, nearY = 0.0f;
    Check(ProjectFromRenderedEye(kNoRotation, Aim(eye, ue::FVector{100.0, 0.0, 0.0}),
                                 kTan, kTan, nearX, nearY), "near lean projects");
    CheckNear(nearX, -lean / 100.0, "lean at 1m: ndc x is -lean/distance");
    CheckNear(nearY, 0.0, "lean at 1m: pure X lean does not move y");

    float farX = 0.0f, farY = 0.0f;
    Check(ProjectFromRenderedEye(kNoRotation, Aim(eye, ue::FVector{1000.0, 0.0, 0.0}),
                                 kTan, kTan, farX, farY), "far lean projects");
    CheckNear(farX, -lean / 1000.0, "lean at 10m: ndc x is -lean/distance");

    // The gate itself: the two ranges MUST disagree. A fixed or stale depth
    // would make them equal, and that is the failure the doctrine describes as
    // agreeing at exactly one distance.
    Check(std::fabs(nearX - farX) > 0.2f,
          "near and far leans give different offsets (no fixed depth)");
}

void TestOppositeLeansAreSymmetric() {
    const ue::FVector point{300.0, 0.0, 0.0};
    float leftX = 0.0f, leftY = 0.0f, rightX = 0.0f, rightY = 0.0f;
    Check(ProjectFromRenderedEye(kNoRotation, Aim(ue::FVector{0.0, -25.0, 0.0}, point),
                                 kTan, kTan, leftX, leftY), "left lean projects");
    Check(ProjectFromRenderedEye(kNoRotation, Aim(ue::FVector{0.0, 25.0, 0.0}, point),
                                 kTan, kTan, rightX, rightY), "right lean projects");
    CheckNear(leftX, -rightX, "opposite leans give opposite offsets");
    CheckNear(leftY, 0.0, "left lean does not move y");
    CheckNear(rightY, 0.0, "right lean does not move y");
}

void TestVerticalLeanMovesOnlyY() {
    const ue::FVector point{200.0, 0.0, 0.0};
    float x = 0.0f, y = 0.0f;
    Check(ProjectFromRenderedEye(kNoRotation, Aim(ue::FVector{0.0, 0.0, 20.0}, point),
                                 kTan, kTan, x, y), "rise projects");
    CheckNear(x, 0.0, "pure Y lean does not move x");
    // Rising 20cm puts the surface below the eye, so the mark goes DOWN.
    CheckNear(y, -20.0 / 200.0, "pure Y lean moves y by -lean/distance");
}

void TestPureRollLeavesTheMarkAtTheCentre() {
    // Roll about the view axis, no lean and no pitch. The point is on that axis,
    // so it stays at the centre however far the head tilts - and the projection
    // has to say so without any roll term of its own, because the basis is what
    // carries the roll.
    for (double rollDeg : {-40.0, -5.0, 5.0, 40.0}) {
        const ue::FQuat4d q = ue::QuatFromEulerDeg(0.0, 0.0, rollDeg);
        float x = 1.0f, y = 1.0f;
        Check(ProjectFromRenderedEye(q, ue::FVector{500.0, 0.0, 0.0}, kTan, kTan, x, y),
              "roll projects");
        CheckNear(x, 0.0, "pure roll: mark stays at screen centre (x)");
        CheckNear(y, 0.0, "pure roll: mark stays at screen centre (y)");
    }
}

void TestPurePitchMovesOnlyVertically() {
    // Head pitched up by 10 degrees with the aim still level: the mark drops by
    // tan(10) and does not wander sideways.
    const ue::FQuat4d q = ue::QuatFromEulerDeg(10.0, 0.0, 0.0);
    float x = 1.0f, y = 1.0f;
    Check(ProjectFromRenderedEye(q, ue::FVector{400.0, 0.0, 0.0}, kTan, kTan, x, y),
          "pitch projects");
    CheckNear(x, 0.0, "pure pitch does not move x");
    CheckNear(y, -std::tan(10.0 * 3.14159265358979323846 / 180.0),
              "pure pitch moves y by -tan(pitch)");
}

void TestPitchAndRollDoNotWanderHorizontally() {
    // The combined pose that catches a per-axis Euler formula, and the one place
    // a roll SIGN error shows up. Both components are pinned, not the radius:
    // roll rotates (x, y) within the plane perpendicular to the view axis, so the
    // radius is invariant under roll by construction and asserting it locks
    // nothing. Flip the roll sign anywhere between the camera write and the
    // projection basis and the radius is unchanged while x crosses the centre -
    // 0.110 NDC at 15 degrees, which is 106 px on a 1920-wide frame. That is the
    // fault the doctrine records as having shipped twice.
    const double kPi = 3.14159265358979323846;
    const double pitch = 12.0;
    const double expected = std::tan(pitch * kPi / 180.0);
    for (double rollDeg : {0.0, 15.0, -30.0, 60.0}) {
        const ue::FQuat4d q = ue::QuatFromEulerDeg(pitch, 0.0, rollDeg);
        float x = 0.0f, y = 0.0f;
        Check(ProjectFromRenderedEye(q, ue::FVector{400.0, 0.0, 0.0}, kTan, kTan, x, y),
              "pitch+roll projects");
        const double r = rollDeg * kPi / 180.0;
        CheckNear(x,  expected * std::sin(r), "pitch+roll: x follows sin(roll)");
        CheckNear(y, -expected * std::cos(r), "pitch+roll: y follows -cos(roll)");
        CheckNear(std::sqrt(static_cast<double>(x) * x + static_cast<double>(y) * y),
                  expected, "pitch+roll: the mark stays the pitch's distance from centre");
    }
}

// The two half-field tangents are forwarded in order, and every other case here
// passes the same value for both, so a swap between them is invisible. On the
// 32:9 viewport the axis-model case below covers, the two differ by the aspect -
// a swap scales the horizontal offset by 3.56x and throws the mark clean off the
// frame on any head yaw.
void TestTheTwoFieldTangentsAreNotInterchangeable() {
    float x = 0.0f, y = 0.0f;
    Check(ProjectFromRenderedEye(kNoRotation, Aim(ue::FVector{0.0, 30.0, 0.0},
                                                  ue::FVector{100.0, 0.0, 20.0}),
                                 2.0f, 0.5f, x, y),
          "asymmetric tangents project");
    // x = (aim.Y / aim.X) / tanX = (-30/100) / 2, y = (aim.Z / aim.X) / tanY.
    CheckNear(x, -0.30 / 2.0, "the horizontal offset divides by tanX");
    CheckNear(y,  (20.0 / 100.0) / 0.5, "the vertical offset divides by tanY");
}

// The band either side of the guard, not just a point well past it. The
// projection refuses at z <= 0.1 of a unit aim vector, which is 84.26 degrees -
// not the 90 the single 100-degree probe below would suggest. Widening the guard
// would make the mark vanish on a routine head turn; narrowing it would hand the
// caller an NDC in the thousands.
void TestAimBehindGuardBand() {
    float x = 0.0f, y = 0.0f;
    Check(ProjectFromRenderedEye(ue::QuatFromEulerDeg(0.0, 80.0, 0.0),
                                 ue::FVector{400.0, 0.0, 0.0}, kTan, kTan, x, y),
          "80 degrees off axis still projects");
    Check(!ProjectFromRenderedEye(ue::QuatFromEulerDeg(0.0, 86.0, 0.0),
                                  ue::FVector{400.0, 0.0, 0.0}, kTan, kTan, x, y),
          "86 degrees off axis is past the guard and is rejected");
}

void TestAimBehindTheViewIsRejected() {
    // Head turned right past 90 degrees off the aim: there is no screen position
    // for it, so nothing is moved rather than a mark thrown off to one side.
    const ue::FQuat4d q = ue::QuatFromEulerDeg(0.0, 100.0, 0.0);
    float x = 0.0f, y = 0.0f;
    Check(!ProjectFromRenderedEye(q, ue::FVector{400.0, 0.0, 0.0}, kTan, kTan, x, y),
          "aim behind the rendered view is rejected");
}

void TestFovAxisModels() {
    // A 32:9 display is where the two models are furthest apart, which is what
    // makes assuming one of them a visible bug rather than a rounding error.
    const float aspect = 5120.0f / 1440.0f;
    float xa = 0.0f, ya = 0.0f, xb = 0.0f, yb = 0.0f;

    Check(fov::HalfFieldTangents(fov::kMaintainXFOV, 90.0f, aspect, xa, ya),
          "MaintainXFOV resolves");
    CheckNear(xa, 1.0, "MaintainXFOV: the scalar is the horizontal field");
    CheckNear(ya, 1.0 / aspect, "MaintainXFOV: vertical narrows with the display");

    Check(fov::HalfFieldTangents(fov::kMaintainYFOV, 90.0f, aspect, xb, yb),
          "MaintainYFOV resolves");
    CheckNear(yb, 1.0, "MaintainYFOV: the scalar is the vertical field");
    CheckNear(xb, aspect, "MaintainYFOV: horizontal widens with the display");

    // MajorAxisFOV follows whichever axis is longer.
    float xm = 0.0f, ym = 0.0f;
    Check(fov::HalfFieldTangents(fov::kMajorAxisFOV, 90.0f, aspect, xm, ym),
          "MajorAxisFOV resolves on a landscape display");
    CheckNear(xm, xa, "MajorAxisFOV on a landscape display matches MaintainXFOV");
    Check(fov::HalfFieldTangents(fov::kMajorAxisFOV, 90.0f, 0.5f, xm, ym),
          "MajorAxisFOV resolves on a portrait display");
    CheckNear(ym, 1.0, "MajorAxisFOV on a portrait display holds the vertical field");

    // Unknown constraint projects nothing. At this aspect the two models are
    // 3.6x apart horizontally, so guessing is worse than not drawing.
    float xu = 0.0f, yu = 0.0f;
    Check(!fov::HalfFieldTangents(fov::kConstraintUnknown, 90.0f, aspect, xu, yu),
          "an unread aspect constraint projects nothing");
    Check(!fov::HalfFieldTangents(fov::kMaintainXFOV, 0.0f, aspect, xu, yu),
          "an unread field of view projects nothing");
    Check(!fov::HalfFieldTangents(fov::kMaintainXFOV, 90.0f, 0.0f, xu, yu),
          "a viewport with no shape projects nothing");
}

}  // namespace

int main() {
    TestCentredHeadMarksTheCentre();
    TestLeanParallaxScalesWithDistance();
    TestOppositeLeansAreSymmetric();
    TestVerticalLeanMovesOnlyY();
    TestPureRollLeavesTheMarkAtTheCentre();
    TestPurePitchMovesOnlyVertically();
    TestPitchAndRollDoNotWanderHorizontally();
    TestTheTwoFieldTangentsAreNotInterchangeable();
    TestAimBehindTheViewIsRejected();
    TestAimBehindGuardBand();
    TestFovAxisModels();

    if (g_failures == 0) {
        std::printf("projection tests: all passed\n");
        return 0;
    }
    std::printf("projection tests: %d failure(s)\n", g_failures);
    return 1;
}
