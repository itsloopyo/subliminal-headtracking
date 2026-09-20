// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

#include <cstdint>

#include <cameraunlock/unreal/ue_math.h>

// Point the flashlight down the head-tracked view.
//
// The beam does not follow the head on its own, and the reason is in the
// component chain the character carries:
//
//     Main Arm (SpringArmComponent, bUsePawnControlRotation)
//       └── CineCamera
//             └── FlashlightArm (SpringArmComponent, bUsePawnControlRotation,
//                 │              CameraRotationLagSpeed 15)
//                 └── FlashlightLightSpot
//
// A spring arm with bUsePawnControlRotation builds its socket from the pawn's
// CONTROL rotation every tick, not from its parent, so the light hangs off the
// mouse aim and never sees the view hook's injected pose. Measured in game: the
// arm's own RelativeRotation sits at a static (0, 90, 0) while the control
// rotation swings underneath it, and the light's world rotation tracks the
// camera's exactly.
//
// What the light DOES own is its own relative transform, which the engine
// composes onto that socket and which nothing in the game writes - it reads
// (0, 0, 0) through a whole session. So the head pose goes there, as the delta
// between the clean camera and the tracked one, and the beam lands where the
// player is looking. No VM dispatch and no new RVA: the arm ticks its children
// every frame, which is what republishes the write to the renderer.
namespace subliminal_ht::flashlight {

// Aim the beam for this frame.
//
// `beamQ` is the rotation the beam should end up at: the head pose SCALED by the
// light multiplier and composed onto the clean rotation by the same function the
// camera went through. Composed by the caller rather than here, so the beam and
// the view cannot disagree about which way the head turned - the property a
// player can actually see, and the reason core's head_follow_light.h says to
// match the camera's composition rather than pick the tidier maths.
//
// `leanWorld` is the eye offset the lean clamp actually allowed, so the beam
// leaves the eye the frame is drawn from. It is NOT scaled by the multiplier:
// starting the beam further out than the eye went is the fixed-parallax error
// the reticle doctrine describes, agreeing with the view at one distance and
// splaying either side of it.
//
// A pawn with no flashlight resolves to nothing and costs two loads.
void Follow(std::uintptr_t pawn,
            const cameraunlock::unreal::FQuat4d& cleanQ,
            const cameraunlock::unreal::FQuat4d& beamQ,
            const cameraunlock::unreal::FVector& leanWorld);

// Give the beam back to the game, exactly as it was found. Called from every
// stand-down, because a beam left holding its last tracked offset over an
// untracked view is the flashlight having come loose, not tracking having
// stopped.
void Release();

}  // namespace subliminal_ht::flashlight
