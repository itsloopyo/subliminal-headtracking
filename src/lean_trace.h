// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

#include <cstdint>

#include <cameraunlock/camera/lean_clamp.h>
#include <cameraunlock/math/vec3.h>

// The engine half of the lean collision clamp: ask the game how far the eye may
// travel from where it put the camera before it meets something solid.
//
// Core owns what to do with the answer (cameraunlock/camera/lean_clamp.h); this
// owns getting one. The sweep goes through the engine's own
// UKismetSystemLibrary::SphereTraceSingle, dispatched by name through the script
// VM with its parameter frame read out of the engine's reflection data - the
// same route aim_trace takes - so there is no second physics query in this mod
// to disagree with the game's, and no RVA to re-pin every patch.
//
// A ZERO-EXTENT ray rather than a swept sphere, and the standoff is applied to
// the answer rather than carried by the shape. A sphere would give the standoff
// for free, but it reports an initial overlap whenever the eye already sits
// within its radius of anything - routine in first person - and UE reports that
// as a blocking hit at zero distance, which refuses the lean in every direction
// at once. The two cases could be told apart by FHitResult::bStartPenetrating,
// except that UE packs it into the same byte as bBlockingHit, so both resolve to
// a single FProperty offset and only an FBoolProperty ByteMask separates them -
// which this mod's reflection cannot read. The ray removes the ambiguity rather
// than deciding it. The cost is that a ray can thread a gap a sphere would not.
namespace subliminal_ht::lean_trace {

// How far off a blocking surface the eye is held, in UE units (cm). It must
// exceed the camera's near clip distance or geometry is culled before the eye
// reaches it and the wall goes transparent anyway.
void SetRadius(float centimetres);

// Which ETraceTypeQuery the sweep runs on. Configurable because the channel a
// level's geometry blocks is a project setting, not an engine constant, and the
// only way to know is to run it and read the log.
void SetChannel(int traceTypeQuery);

// Resolve the sweep and FHitResult against the live reflection data. Safe to
// call every frame - it does the work once and answers from cache.
bool Ready();

// The pawn to sweep from, set once per frame by the view hook.
//
// The pawn rather than the controller, and it matters: it is passed as the
// sweep's WorldContextObject with bIgnoreSelf set, which is what excludes the
// player's own capsule. The eye sits inside that capsule, so without it every
// sweep starts penetrating and reports nowhere to go - the lean would simply
// stop working, everywhere, with no obvious cause.
void SetPawn(std::uintptr_t pawn);

// The query, in the shape core's clamp takes. `context` is unused - the pawn
// comes from SetPawn - and is present to match LeanQueryFn. Game thread only.
cameraunlock::camera::LeanObstruction Query(void* context,
                                            const cameraunlock::math::Vec3& start,
                                            const cameraunlock::math::Vec3& direction,
                                            float maxDistance);

}  // namespace subliminal_ht::lean_trace
