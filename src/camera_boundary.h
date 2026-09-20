// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

#include <cameraunlock/unreal/ue_math.h>

// Where the tracker's convention meets Unreal's.
//
// Both conversions below are pure functions of their arguments - no globals, no
// engine reads - so the numbers that took an in-game session to settle can be
// diffed against a sibling mod's and exercised by tests. Everything that talks
// to the live process lives above this, in view_hook.cpp.
namespace subliminal_ht::camera_boundary {

// Compose the head pose (degrees, tracker convention) onto a clean engine
// rotation, in place.
//
// worldSpaceYaw true adds the pose to the FRotator directly, so yaw turns about
// the world up-axis and the horizon stays level on a pitched turn. False
// post-multiplies a camera-local quaternion, which leans the horizon instead.
// Roll is negated either way: OpenTrack calls the other direction positive.
//
// The numbers are not re-derived here. The signs and the composition are a port
// of still-wakes-the-deep-headtracking's, which bodycam-headtracking and
// what-remains-of-edith-finch-headtracking also match: shipped UE5 mods behind
// the same GetPlayerViewPoint hook, whose signs were settled in a running game.
// Deriving them again from handedness would throw that away and land a coin flip
// per axis in front of the player.
//
// ONE deliberate divergence from those ancestors, so a diff against them is not
// a surprise: the world-yaw branch clamps the composed pitch. They add the head
// pitch to the base rotation unbounded, and this game lets the player look to
// within a tenth of a degree of straight down, so an ordinary downward tilt
// composes past -90 and the rendered horizon rolls over the top. It belongs
// upstream in all of them.
void ApplyHeadPose(cameraunlock::unreal::FRotator& rotation,
                   double yaw, double pitch, double roll, bool worldSpaceYaw);

// Build a world-space camera-location offset (UE units = cm) from the session's
// processed offset (metres) in the CLEAN camera frame, so head sway follows the
// body rather than the head-rotated view.
cameraunlock::unreal::FVector PositionOffset(
    const cameraunlock::unreal::FQuat4d& cleanRotation,
    float offsetX, float offsetY, float offsetZ);

}  // namespace subliminal_ht::camera_boundary
