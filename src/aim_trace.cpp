// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "aim_trace.h"

#include <cstring>
#include <string>
#include <vector>

#include <windows.h>

#include "kismet_trace.h"
#include "logging.h"
#include "ue_objects.h"
#include "ue_reflect.h"
#include "ue_vm.h"

#include "cameraunlock/unreal/ue_runtime.h"

namespace subliminal_ht::aim_trace {

namespace {

namespace ue = ::cameraunlock::unreal;

using kismet_trace::ExpectedWidth;
using kismet_trace::TArrayHeader;
using kismet_trace::kMaxParams;

ue_vm::ResolveRetry g_resolveRetry;

struct Layout {
    std::size_t ParamsSize = 0;
    std::size_t WorldContext = 0;
    std::size_t Start = 0;
    std::size_t End = 0;
    std::size_t TraceChannel = 0;
    std::size_t TraceComplex = 0;
    std::size_t ActorsToIgnore = 0;
    std::size_t DrawDebugType = 0;
    std::size_t OutHit = 0;
    std::size_t IgnoreSelf = 0;
    std::size_t ReturnValue = 0;
    // Inside FHitResult.
    std::size_t ImpactPoint = 0;
};

Layout g_layout;
std::uintptr_t g_lineTraceFn = 0;
std::uintptr_t g_kismetSystemCdo = 0;
bool g_ready = false;
bool g_failed = false;
bool g_dispatchFaultLogged = false;
int  g_traceChannel = 0;

// One ignored actor, pointed at by the TArray the parameter frame carries.
// LineTraceSingle takes ActorsToIgnore by const reference and never
// reallocates it, so a static backing store is enough and there is nothing for
// the engine to free.
std::uintptr_t g_ignoreStorage = 0;

bool Resolve() {
    if (g_ready) return true;
    if (g_failed) return false;
    if (!g_resolveRetry.Due()) {
        // Out of attempts with nothing resolved. Every other give-up here says
        // so in the log; this path has to as well, or the reticle silently drops
        // to the rotation-only projection - which looks fine standing still and
        // slides off the target the moment the head leans.
        if (g_resolveRetry.Exhausted()) {
            Log::Line("aim-trace: LineTraceSingle never appeared in the object table - "
                      "standing the parallax down. The crosshair still follows head "
                      "rotation, but it will not track the interaction point on a lean.");
            g_failed = true;
        }
        return false;
    }
    if (!ue_vm::Ready()) return false;

    g_kismetSystemCdo = kismet_trace::FindKismetSystemLibraryCdo();
    g_lineTraceFn = ue_objects::FindLiveObject("Function", "LineTraceSingle", "KismetSystemLibrary");
    if (!g_kismetSystemCdo || !g_lineTraceFn) {
        // The object table is walked from the render caller, so the first frames
        // can run before these are visible. Only repeated failure is a fault,
        // and the exhaustion check above turns that into a give-up.
        return false;
    }

    static const std::vector<std::string> kParams = {
        "WorldContextObject", "Start", "End", "TraceChannel", "bTraceComplex",
        "ActorsToIgnore", "DrawDebugType", "OutHit", "bIgnoreSelf", "ReturnValue",
    };
    std::vector<ue_reflect::FieldInfo> p;
    if (!ue_reflect::ResolveAll("LineTraceSingle", g_lineTraceFn, kParams, p)) {
        g_failed = true;
        return false;
    }

    const std::size_t paramsSize = ue_reflect::StructSize(g_lineTraceFn);
    if (paramsSize > kMaxParams) {
        Log::Line("aim-trace: LineTraceSingle parameter frame is %zu bytes, which is "
                  "not a Kismet trace frame - standing the parallax down", paramsSize);
        g_failed = true;
        return false;
    }

    static const ExpectedWidth kWidths[] = {
        {"WorldContextObject", 0, sizeof(std::uintptr_t)},
        {"Start",              1, sizeof(ue::FVector)},
        {"End",                2, sizeof(ue::FVector)},
        {"TraceChannel",       3, 1},
        {"bTraceComplex",      4, 1},
        {"ActorsToIgnore",     5, sizeof(TArrayHeader)},
        {"DrawDebugType",      6, 1},
        {"bIgnoreSelf",        8, 1},
        {"ReturnValue",        9, 1},
    };
    if (!kismet_trace::FrameFitsWrites("aim-trace: LineTraceSingle", p, kWidths,
                                       paramsSize)) {
        Log::Line("aim-trace: standing the parallax down");
        g_failed = true;
        return false;
    }

    // ImpactPoint alone. FHitResult::Distance is deliberately not used - Cast
    // projects the contact position onto the aim direction instead - so
    // requiring it to resolve would stand the whole parallax down over a field
    // this file had already decided not to trust.
    const std::uintptr_t hitResult = ue_objects::FindLiveObject("ScriptStruct", "HitResult", nullptr);
    std::vector<ue_reflect::FieldInfo> h;
    if (!hitResult ||
        !ue_reflect::ResolveAll("HitResult", hitResult, {"ImpactPoint"}, h)) {
        Log::Line("aim-trace: FHitResult layout did not resolve - standing the "
                  "parallax down");
        g_failed = true;
        return false;
    }
    // ImpactPoint is an FVector under Large World Coordinates: three doubles.
    // Anything else says the field found is not the one meant, and reading three
    // doubles out of it would produce a plausible-looking aim point made of
    // whatever follows.
    if (h[0].Size != sizeof(ue::FVector)) {
        Log::Line("aim-trace: FHitResult::ImpactPoint is %zu bytes, expected %zu "
                  "(LWC FVector) - standing the parallax down",
                  h[0].Size, sizeof(ue::FVector));
        g_failed = true;
        return false;
    }

    // OutHit is read at OutHit + ImpactPoint, so the slot has to hold a whole
    // FHitResult rather than merely start inside the frame.
    const std::size_t hitSize = ue_reflect::StructSize(hitResult);
    if (!ue_reflect::FieldFits(p[7], hitSize, paramsSize)) {
        Log::Line("aim-trace: LineTraceSingle.OutHit is %zu bytes at +0x%zx in a %zu-byte "
                  "frame and FHitResult is %zu bytes - standing the parallax down",
                  p[7].Size, p[7].Offset, paramsSize, hitSize);
        g_failed = true;
        return false;
    }

    g_layout.ParamsSize     = paramsSize;
    g_layout.WorldContext   = p[0].Offset;
    g_layout.Start          = p[1].Offset;
    g_layout.End            = p[2].Offset;
    g_layout.TraceChannel   = p[3].Offset;
    g_layout.TraceComplex   = p[4].Offset;
    g_layout.ActorsToIgnore = p[5].Offset;
    g_layout.DrawDebugType  = p[6].Offset;
    g_layout.OutHit         = p[7].Offset;
    g_layout.IgnoreSelf     = p[8].Offset;
    g_layout.ReturnValue    = p[9].Offset;
    g_layout.ImpactPoint    = h[0].Offset;

    Log::Line("aim-trace: LineTraceSingle frame=%zu Start=+0x%zx End=+0x%zx "
              "OutHit=+0x%zx Return=+0x%zx | FHitResult ImpactPoint=+0x%zx "
              "| channel=%d",
        g_layout.ParamsSize, g_layout.Start, g_layout.End, g_layout.OutHit,
        g_layout.ReturnValue, g_layout.ImpactPoint, g_traceChannel);
    g_ready = true;
    return true;
}

}  // namespace

void SetTraceChannel(int channel) { g_traceChannel = channel; }

Result Cast(std::uintptr_t pawn, const ue::FVector& start, const ue::FVector& dir,
            double maxDistance) {
    Result r;
    if (!Resolve() || pawn == 0) return r;

    // alignas: ProcessEvent hands this frame to engine code that reads its
    // fields at their natural alignment, and the frame carries LWC doubles.
    alignas(16) unsigned char buf[kMaxParams];
    std::memset(buf, 0, g_layout.ParamsSize);

    // bIgnoreSelf makes LineTraceSingle exclude the actor it is handed as the
    // world context, which is why the PAWN goes here rather than the controller:
    // the ray starts at the eye, inside the player's own capsule, and without
    // this it reports an initial overlap at zero distance every single frame.
    *reinterpret_cast<std::uintptr_t*>(buf + g_layout.WorldContext) = pawn;
    *reinterpret_cast<ue::FVector*>(buf + g_layout.Start) = start;
    *reinterpret_cast<ue::FVector*>(buf + g_layout.End) = ue::FVector{
        start.X + dir.X * maxDistance,
        start.Y + dir.Y * maxDistance,
        start.Z + dir.Z * maxDistance,
    };
    buf[g_layout.TraceChannel] = static_cast<unsigned char>(g_traceChannel);
    buf[g_layout.TraceComplex] = 1;   // per-triangle, so the point is on the surface drawn
    buf[g_layout.DrawDebugType] = 0;  // EDrawDebugTrace::None
    buf[g_layout.IgnoreSelf] = 1;

    g_ignoreStorage = pawn;
    TArrayHeader ignore{ &g_ignoreStorage, 1, 1 };
    std::memcpy(buf + g_layout.ActorsToIgnore, &ignore, sizeof(ignore));

    if (!ue_vm::Dispatch(reinterpret_cast<void*>(g_kismetSystemCdo),
                         reinterpret_cast<void*>(g_lineTraceFn), buf)) {
        // A fault here leaves Valid false, which the caller renders the same way
        // as a definite no-hit: the rotation-only projection. The two are not
        // the same thing and the log has to separate them, or a reticle that has
        // stopped carrying parallax is indistinguishable from one aimed at the
        // sky. Once is enough - it faults every frame or not at all.
        if (!g_dispatchFaultLogged) {
            g_dispatchFaultLogged = true;
            Log::Line("aim-trace: LineTraceSingle faulted - the crosshair falls back to "
                      "head rotation alone and will not track the interaction point "
                      "on a lean");
        }
        return r;
    }

    r.Valid = true;
    r.Hit = buf[g_layout.ReturnValue] != 0;
    if (r.Hit) {
        std::memcpy(&r.Point, buf + g_layout.OutHit + g_layout.ImpactPoint,
                    sizeof(ue::FVector));
        // Measured from the contact's own world position projected onto the aim
        // direction, not from the FHitResult Distance field: a position
        // projected onto a direction is a distance by construction, while a
        // field named Distance is whatever the engine chose to put there.
        r.Distance = (r.Point.X - start.X) * dir.X
                   + (r.Point.Y - start.Y) * dir.Y
                   + (r.Point.Z - start.Z) * dir.Z;
    }
    return r;
}

}  // namespace subliminal_ht::aim_trace
