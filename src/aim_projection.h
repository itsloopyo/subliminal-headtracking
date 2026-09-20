// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

#include <cmath>

#include <cameraunlock/rendering/aim_ndc_projection.h>
#include <cameraunlock/unreal/ue_math.h>

// Where the aim ray lands, in the picture the head is looking at.
//
// The game draws its ring at the centre of the frame, so it marks the ray only
// while the rendered view IS the aim. The moment the head turns or leans, the
// two are different and the ring is a lie.
//
// The projection takes a vector FROM THE EYE THE FRAME IS DRAWN FROM TO THE
// POINT THE RAY STOPS ON, and the basis the frame is actually rendered with,
// and asks where the first lands in the second. A POINT and not a direction on
// purpose: with the head centred the two project identically, but a lean draws
// the frame from an eye up to 30cm to one side of the one the ray leaves from,
// and a projection built on the direction then slides off the thing it marks,
// worse the closer the surface.
//
// It is basis-to-basis rather than a formula in yaw / pitch / roll, so there is
// no second derivation of the camera composition to disagree with the first:
// the hook hands over the quaternion it actually wrote.
namespace subliminal_ht::aim_projection {

namespace ue = ::cameraunlock::unreal;

// Everything one rendered frame contributes, in engine units and the engine's
// own basis.
struct Frame {
    // The eye the frame is drawn from - the clean camera position plus whatever
    // lean survived the collision clamp. The parallax term is measured from
    // here, which is what makes it the leaned eye and not the clean one.
    ue::FVector RenderedEye{0.0, 0.0, 0.0};
    // The rotation the hook wrote into the view, as a quaternion. The frame's
    // forward / right / up come from this and nowhere else.
    ue::FQuat4d TrackedRotation{0.0, 0.0, 0.0, 1.0};
    // The clean aim direction, unit length. Used when the ray hit nothing: a
    // target at infinity has no parallax, and its direction is the whole answer.
    ue::FVector Direction{1.0, 0.0, 0.0};
    // Where the ray stopped, in world space, when it stopped on something.
    ue::FVector Point{0.0, 0.0, 0.0};
    bool HasPoint = false;
};

// Project a world-space vector measured from the rendered eye into the frame
// the given rotation draws, as normalised device coordinates (x right, y up,
// both -1..1 across the frame).
//
// Pure and header-only so the tests can lock it without a game. UE's camera
// basis is X forward, Y right, Z up, and the vectors come from the same
// quaternion the hook wrote, so this cannot disagree with the camera about
// which way the frame is facing.
inline bool ProjectFromRenderedEye(const ue::FQuat4d& trackedRotation,
                                   const ue::FVector& v,
                                   float tanX, float tanY,
                                   float& ndcX, float& ndcY) {
    const ue::FVector fwd   = ue::QuatRotateVec(trackedRotation, ue::FVector{1.0, 0.0, 0.0});
    const ue::FVector right = ue::QuatRotateVec(trackedRotation, ue::FVector{0.0, 1.0, 0.0});
    const ue::FVector up    = ue::QuatRotateVec(trackedRotation, ue::FVector{0.0, 0.0, 1.0});

    // Normalised, because core's behind-the-view guard is a threshold on the
    // UNNORMALISED forward dot product. This mod feeds it vectors of two very
    // different scales - a hit point is centimetres from the eye, a no-hit
    // direction is unit length - so the same guard would mean 90 degrees on one
    // and 84.3 on the other, and the same head angle would invalidate the mark
    // or not depending on whether the ray happened to land on something. The
    // projection is a ratio, so scaling v changes nothing else.
    const double len = std::sqrt(v.X * v.X + v.Y * v.Y + v.Z * v.Z);
    if (!(len > 0.0)) return false;
    const double inv = 1.0 / len;

    const float aimF[3]   = {static_cast<float>(v.X * inv),
                             static_cast<float>(v.Y * inv),
                             static_cast<float>(v.Z * inv)};
    const float fwdF[3]   = {static_cast<float>(fwd.X),   static_cast<float>(fwd.Y),   static_cast<float>(fwd.Z)};
    const float rightF[3] = {static_cast<float>(right.X), static_cast<float>(right.Y), static_cast<float>(right.Z)};
    const float upF[3]    = {static_cast<float>(up.X),    static_cast<float>(up.Y),    static_cast<float>(up.Z)};

    return cameraunlock::rendering::ProjectAimToNdc(aimF, fwdF, rightF, upF,
                                                    tanX, tanY, ndcX, ndcY);
}

// Publish one render-caller frame. active=false invalidates the offset entirely
// (tracking off, no tracker data, or not in gameplay). fovDegrees is the field
// of view read off the FMinimalViewInfo the frame is being built into; a value
// outside camera_fov's plausible bracket invalidates the projection rather than
// leaving it running on the last good one.
void Update(const Frame& frame, float fovDegrees, bool active);

// Refresh only the active flag without disturbing the cached frame, for the
// non-render GetPlayerViewPoint callers the caller gate rejects.
void SetActive(bool active);

// Where the ray lands, as a pixel offset from the centre of the viewport
// (+x right, +y down). False when there is no valid offset, in which case dx
// and dy are untouched and the caller must not move anything.
bool GetScreenOffset(float& dx, float& dy);

}  // namespace subliminal_ht::aim_projection
