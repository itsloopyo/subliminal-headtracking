// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

#include <cstdint>

#include <cameraunlock/unreal/ue_math.h>

// Where the aim ray stops, this frame.
//
// The reticle marks a POINT, not a direction. With the eye on the ray itself
// the two project to the same pixel, which is why a direction is enough for a
// mod that only tracks rotation; a lean up to 30cm off that ray separates them,
// and a
// reticle built on a direction then slides off the thing it marks, worse the
// closer the target. So the parallax term needs the live distance to whatever
// the player is pointing at, and it has to be THIS frame's distance: a fixed,
// smoothed or stale depth leaves an error proportional to
// lean * (1/d0 - 1/d), which is zero at exactly one range and changes sign
// either side of it.
//
// The cast runs through the engine's own UKismetSystemLibrary::LineTraceSingle
// so there is no second physics query implementation in this mod to disagree
// with the game's, and its parameter frame is read out of the engine's
// reflection data rather than assumed - see ue_reflect.h.
namespace subliminal_ht::aim_trace {

struct Result {
    // False when the trace could not be performed at all: the reflection data
    // did not resolve, the VM is not up, or the dispatch faulted. Never a stale
    // point - Point is left at the origin, so a caller cannot accidentally reuse
    // the last wall.
    //
    // The caller drops to the rotation-only projection, the same branch a
    // definite no-hit takes, and that is the better of the two available
    // answers rather than a shrug: it loses the parallax term while the head is
    // leaning, whereas refusing to project parks the ring at dead screen centre,
    // which is wrong by the whole head turn. Both are logged - see the dispatch
    // fault line in Cast, and the resolve give-up.
    bool Valid = false;
    // True when something blocked the ray. False is a definite no-hit, which is
    // a target at infinity: the caller projects the aim DIRECTION for that frame
    // rather than substituting a magic distance.
    bool Hit = false;
    cameraunlock::unreal::FVector Point{0.0, 0.0, 0.0};
    double Distance = 0.0;   // along `dir` from `start`, in UE units (cm)
};

// Cast from `start` along `dir` (unit vector) for `maxDistance` UE units.
// `pawn` is the actor to exclude, so the ray does not stop on the player's own
// body a centimetre in front of the eye. Must be called on the game thread.
//
// Resolves the UFunction, its parameter frame and FHitResult's fields on the
// first call that can, logging exactly which lookup failed when the layout does
// not fit; the caller then gets an invalid Result and runs the rotation-only
// reticle.
Result Cast(std::uintptr_t pawn,
            const cameraunlock::unreal::FVector& start,
            const cameraunlock::unreal::FVector& dir,
            double maxDistance);

// ETraceTypeQuery index the cast uses. Configurable because which query channel
// a game's bullets stop on is a project setting, not an engine constant, and
// the only way to know is to fire at a wall and compare.
void SetTraceChannel(int channel);

}  // namespace subliminal_ht::aim_trace
