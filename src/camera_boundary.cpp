// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "camera_boundary.h"

namespace subliminal_ht::camera_boundary {

namespace {
namespace ue = ::cameraunlock::unreal;

// UE works in centimetres; the processor hands out metres.
constexpr double kMetresToUE = 100.0;

// APlayerCameraManager's own default ViewPitchMin / ViewPitchMax. Past a quarter
// turn a view pitch has gone over the top and the world is upside down, so the
// engine's own limit is the right one to hold to.
constexpr double kMaxViewPitch = 89.9;
}  // namespace

void ApplyHeadPose(ue::FRotator& rotation,
                   double yaw, double pitch, double roll, bool worldSpaceYaw) {
    if (worldSpaceYaw) {
        rotation.Yaw   += yaw;
        // Clamped, unlike yaw and roll, which wrap harmlessly. Pitch does not:
        // the game already lets the player look within a tenth of a degree of
        // straight down, so any downward head pitch on top of that composes past
        // -90 and the rendered view rolls over the top - the horizon inverts
        // while the head is still tilting one way.
        //
        // The clamp is this branch's alone. The quaternion branch below cannot
        // produce an out-of-range pitch at all, because QuatToRotator takes it
        // out of asin. That is not the same as saying it behaves well at the
        // pole: within a degree of vertical its Euler extraction is singular and
        // a fraction of a degree of head yaw swings the reported yaw and roll by
        // tens of degrees. Clamping pitch would not help there, and no clamp is
        // applied.
        rotation.Pitch += pitch;
        if (rotation.Pitch >  kMaxViewPitch) rotation.Pitch =  kMaxViewPitch;
        if (rotation.Pitch < -kMaxViewPitch) rotation.Pitch = -kMaxViewPitch;
        rotation.Roll  -= roll;
        return;
    }
    const ue::FQuat4d baseQ = ue::QuatFromEulerDeg(rotation.Pitch, rotation.Yaw, rotation.Roll);
    const ue::FQuat4d headQ = ue::QuatFromEulerDeg(pitch, yaw, -roll);
    const ue::FRotator composed = ue::QuatToRotator(ue::QuatMul(baseQ, headQ));
    rotation.Pitch = composed.Pitch;
    rotation.Yaw   = composed.Yaw;
    rotation.Roll  = composed.Roll;
}

ue::FVector PositionOffset(const ue::FQuat4d& cleanRotation,
                           float offsetX, float offsetY, float offsetZ) {
    const ue::FVector camFwd   = ue::QuatRotateVec(cleanRotation, ue::FVector{1.0, 0.0, 0.0});
    const ue::FVector camRight = ue::QuatRotateVec(cleanRotation, ue::FVector{0.0, 1.0, 0.0});
    const ue::FVector camUp    = ue::QuatRotateVec(cleanRotation, ue::FVector{0.0, 0.0, 1.0});

    // The processor's z runs the other way to UE's camera-forward: it clamps to
    // [-limit_z, +limit_z_back], i.e. NEGATIVE z is the forward lean. Negate it
    // here, at the engine boundary, rather than via the processor's invert_z -
    // inversion happens before the clamp, so flipping it there would put the
    // generous 0.40m limit on leaning back and 0.10m on leaning in.
    //
    // Sway is negated for a reason that needs no knowledge of what the tracker
    // calls positive: turning the head rotates the face about the neck, so the
    // tracked point slides sideways by sin(yaw)*pivot, and that slide has to go
    // the same way as the turn. In 13 of 13 logged samples with |yaw| > 3 deg
    // the reported x ran OPPOSITE to the reported yaw, at an implied pivot arm
    // of 7-11 cm (a real neck). Yaw maps straight through (three shipped UE
    // siblings agree), so x is the mirrored one; left un-negated the camera
    // slides right as the view turns left.
    const double surge = -static_cast<double>(offsetZ) * kMetresToUE;  // -> forward
    const double sway  = -static_cast<double>(offsetX) * kMetresToUE;  // -> right
    const double heave =  static_cast<double>(offsetY) * kMetresToUE;  // -> up
    return ue::FVector{
        camFwd.X * surge + camRight.X * sway + camUp.X * heave,
        camFwd.Y * surge + camRight.Y * sway + camUp.Y * heave,
        camFwd.Z * surge + camRight.Z * sway + camUp.Z * heave,
    };
}

}  // namespace subliminal_ht::camera_boundary
