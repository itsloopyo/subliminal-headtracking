// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

#include <cmath>
#include <cstdint>

// The field of view the frame is drawn with, and how the engine spreads that
// one scalar over the two screen axes.
//
// The reticle projection divides by the half-field tangents, so getting either
// wrong puts the mark a proportional distance from where the player is really
// pointing. UE derives both from ULocalPlayer::AspectRatioAxisConstraint and
// the viewport shape, in FSceneView's projection setup:
//
//     if (viewport is wider than tall && constraint == MajorAxisFOV)
//         || constraint == MaintainXFOV:
//             XAxisMultiplier = 1;             YAxisMultiplier = SizeX/SizeY
//     else:   XAxisMultiplier = SizeY/SizeX;   YAxisMultiplier = 1
//     M00 = XAxisMultiplier / tan(FOV/2)   M11 = YAxisMultiplier / tan(FOV/2)
//
// so the scalar is the HORIZONTAL field in the first branch and the VERTICAL
// field in the second, and the two answers differ by the aspect ratio - they
// agree only on a square display. That is why the constraint is read from the
// engine rather than assumed. On this machine's 32:9 panel the two models put
// the mark 3.5x apart horizontally, which is why an unread constraint projects
// nothing rather than guessing.
//
// The mod does not CHANGE the field of view. It reads it for two reasons: the
// reticle has to be projected through the one the frame is drawn with, and the
// head pose has to be scaled so a zoom does not change how far the head moves
// the picture.
//
// Subliminal zooms. The view target is the pawn's CineCameraComponent, and the
// zoom input winds its focal length up from the authored 49mm against a 100mm
// sensor - 2*atan(100/98) = 91.157 degrees - to a field of view of 39.307. That
// is 2.9x of magnification, and without the correction the head would move the
// picture 2.9x as far while the player is zoomed, which reads as the mod's
// sensitivity jumping rather than as a zoom.
//
// APlayerCameraManager::DefaultFOV's 90 is not the base: it is unused, because a
// camera component on the view target supplies the POV's field of view itself.
// The base is the camera component ARCHETYPE's FieldOfView, which is the
// authored value and does not move when the live component's focal length does.
//
// Everything above the engine line here is pure and header-only, so the tangent
// maths runs in tests with no game.
namespace subliminal_ht::camera_fov {

// What counts as a believable field of view, in degrees. Wide enough for every
// realistic FOV, so a value outside it is struct drift after a game patch or an
// uninitialised frame rather than a setting.
constexpr float kMinDegrees = 10.0f;
constexpr float kMaxDegrees = 170.0f;

// Half of one degree in radians: the half-field tangents and the zoom factor
// both take tan(fov/2) with the field of view in degrees.
inline constexpr float kHalfDegreeInRadians = 3.14159265358979323846f / 360.0f;

// EAspectRatioAxisConstraint. -1 is this mod's "not read yet", not an engine
// value.
inline constexpr int kConstraintUnknown = -1;
inline constexpr int kMaintainYFOV = 0;
inline constexpr int kMaintainXFOV = 1;
inline constexpr int kMajorAxisFOV = 2;

// Phrased as a range test rather than its negation so a NaN, which fails every
// comparison, is rejected instead of passed through.
inline bool Plausible(float degrees) {
    return degrees >= kMinDegrees && degrees <= kMaxDegrees;
}

// True when the engine holds the field of view on the horizontal axis, so the
// scalar IS the horizontal FOV and the vertical narrows as the display widens.
inline bool HorizontalIsFixed(int constraint, float viewportAspect) {
    if (constraint == kMaintainXFOV) return true;
    return constraint == kMajorAxisFOV && viewportAspect > 1.0f;
}

// Half-field tangents for the frame: tan(fovX/2) and tan(fovY/2). False -
// project nothing - when the field of view is not usable, the viewport has no
// shape, or the engine's aspect constraint has not been read. There is no
// guess: the two models disagree at every aspect but 1:1, and a mark drawn
// where the player is not pointing is worse than no mark.
inline bool HalfFieldTangents(int constraint, float fovDegrees,
                              float viewportAspect, float& tanX, float& tanY) {
    if (!Plausible(fovDegrees) || !(viewportAspect > 0.0f)) return false;
    if (constraint == kConstraintUnknown) return false;

    const float t = std::tan(fovDegrees * kHalfDegreeInRadians);
    if (!(t > 0.0f)) return false;
    if (HorizontalIsFixed(constraint, viewportAspect)) {
        tanX = t;
        tanY = t / viewportAspect;
    } else {
        tanY = t;
        tanX = t * viewportAspect;
    }
    return tanX > 0.0f && tanY > 0.0f;
}

// ---- the engine side -----------------------------------------------------

// Read the FOV out of the FMinimalViewInfo the render caller is filling in.
// Returns 0 when the two out-params are not fields of one view info, or what is
// there is not a believable angle.
float Read(const void* outLocation, const void* outRotation);

// Ask the engine how it spreads that scalar over the axes, by reading
// ULocalPlayer::AspectRatioAxisConstraint off the controller's own player.
//
// Re-read on an interval rather than cached for the session: the settings menu
// carries an FOVCon switch with LockX and LockY positions, so the player can
// change which axis the field of view describes while the game is running, and
// a stale answer projects the reticle through the wrong model. Safe to call
// every frame - it answers from cache in between. `controller` is the
// APlayerController the view query was made on, and the call must be on the
// game thread.
void ResolveAspectConstraint(std::uintptr_t controller);

// The EAspectRatioAxisConstraint the engine is using, or kConstraintUnknown
// until the read above has succeeded.
int AspectConstraint();

// Resolve the game's un-zoomed field of view from the player camera's
// archetype, which is the authored value and does not move when the live
// component zooms. Costs an object-table walk, so it retries on an interval and
// gives up after a few passes rather than running every frame. Game thread only.
void ResolveBaseFov();

// The game's un-zoomed field of view in degrees, or 0 until it has been read.
// With no base there is no factor, and the pose is passed through unscaled
// rather than scaled by a guess.
float BaseFov();

// How much to scale a pose so its screen displacement is what it would have
// been un-zoomed, given the field of view the frame is being drawn with. 1.0
// when nothing is zoomed, and 1.0 when the base has not been read.
float ZoomFactor(float renderedFovDegrees);

// The last field of view read off a render-caller frame, and the last zoom
// factor derived from it, for the heartbeat.
float GameFov();
float LastZoomFactor();

}  // namespace subliminal_ht::camera_fov
