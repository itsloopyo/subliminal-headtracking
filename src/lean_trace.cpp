// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "lean_trace.h"

#include <cmath>
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

namespace subliminal_ht::lean_trace {

namespace {

namespace ue = ::cameraunlock::unreal;
using cameraunlock::camera::LeanObstruction;
using cameraunlock::math::Vec3;
using kismet_trace::ExpectedWidth;
using kismet_trace::TArrayHeader;
using kismet_trace::kMaxParams;

ue_vm::ResolveRetry g_resolveRetry;

struct Layout {
    std::size_t ParamsSize = 0;
    std::size_t WorldContext = 0;
    std::size_t Start = 0;
    std::size_t End = 0;
    std::size_t Radius = 0;
    std::size_t TraceChannel = 0;
    std::size_t TraceComplex = 0;
    std::size_t ActorsToIgnore = 0;
    std::size_t DrawDebugType = 0;
    std::size_t OutHit = 0;
    std::size_t IgnoreSelf = 0;
    // Inside FHitResult.
    std::size_t BlockingHit = 0;
    std::size_t HitLocation = 0;
    std::size_t ImpactNormal = 0;
};

// Floor on the cosine of the angle between the lean and the surface normal. The
// standoff is measured along the normal, so converting it to a distance along
// the ray divides by that cosine; at a grazing approach the cosine tends to zero
// and the quotient runs away, so it is floored. 0.35 is about 70 degrees off
// head-on.
constexpr float kMinApproachCos = 0.35f;

Layout g_layout;
std::uintptr_t g_sphereTraceFn = 0;
std::uintptr_t g_kismetSystemCdo = 0;
std::uintptr_t g_pawn = 0;
bool  g_ready = false;
bool  g_failed = false;
float g_radius = 12.0f;
int   g_channel = 0;

void GiveUp(const char* what) {
    if (g_failed) return;
    g_failed = true;
    Log::Line("lean-trace: %s. The lean runs UNCLAMPED for this session - head "
              "position still works, but leaning into a wall will put the view "
              "through it.", what);
}

bool Resolve() {
    if (g_ready) return true;
    if (g_failed) return false;
    if (!g_resolveRetry.Due()) {
        // Out of attempts with nothing resolved. Saying so is the whole point:
        // an unresolved sweep means Query is never handed to the clamp at all,
        // so core's own LastQueryFailed stays false and the lean runs
        // unclamped with nothing in the log to distinguish that from a room
        // with no walls in it.
        if (g_resolveRetry.Exhausted())
            GiveUp("the script VM, the KismetSystemLibrary default object or "
                   "SphereTraceSingle never became readable");
        return false;
    }
    if (!ue_vm::Ready()) return false;

    g_kismetSystemCdo = kismet_trace::FindKismetSystemLibraryCdo();
    g_sphereTraceFn =
        ue_objects::FindLiveObject("Function", "SphereTraceSingle", "KismetSystemLibrary");
    if (!g_kismetSystemCdo || !g_sphereTraceFn) {
        // The object table is walked from the render caller, so the first frames
        // can run before these are visible. Only repeated failure is a fault,
        // and the exhaustion check above turns that into a give-up.
        return false;
    }

    static const std::vector<std::string> kParams = {
        "WorldContextObject", "Start", "End", "Radius", "TraceChannel",
        "bTraceComplex", "ActorsToIgnore", "DrawDebugType", "OutHit", "bIgnoreSelf",
    };
    std::vector<ue_reflect::FieldInfo> p;
    if (!ue_reflect::ResolveAll("SphereTraceSingle", g_sphereTraceFn, kParams, p)) {
        GiveUp("SphereTraceSingle's parameter frame did not resolve");
        return false;
    }

    const std::size_t paramsSize = ue_reflect::StructSize(g_sphereTraceFn);
    if (paramsSize > kMaxParams) {
        GiveUp("SphereTraceSingle's parameter frame is not a Kismet trace frame");
        return false;
    }

    static const ExpectedWidth kWidths[] = {
        {"WorldContextObject", 0, sizeof(std::uintptr_t)},
        {"Start",              1, sizeof(ue::FVector)},
        {"End",                2, sizeof(ue::FVector)},
        {"Radius",             3, sizeof(float)},
        {"TraceChannel",       4, 1},
        {"bTraceComplex",      5, 1},
        {"ActorsToIgnore",     6, sizeof(TArrayHeader)},
        {"DrawDebugType",      7, 1},
        {"bIgnoreSelf",        9, 1},
    };
    if (!kismet_trace::FrameFitsWrites("lean-trace: SphereTraceSingle", p, kWidths,
                                       paramsSize)) {
        GiveUp("the sweep's parameter frame is not the shape this mod writes");
        return false;
    }

    const std::uintptr_t hitResult = ue_objects::FindLiveObject("ScriptStruct", "HitResult", nullptr);
    std::vector<ue_reflect::FieldInfo> h;
    if (!hitResult ||
        !ue_reflect::ResolveAll("HitResult", hitResult,
                                {"bBlockingHit", "Location", "ImpactNormal"}, h)) {
        GiveUp("FHitResult's layout did not resolve");
        return false;
    }
    // Location, and for a zero-extent trace it is the contact point itself - the
    // standoff is applied here rather than coming free from a sweep radius.
    if (h[1].Size != sizeof(ue::FVector)) {
        Log::Line("lean-trace: FHitResult::Location is %zu bytes, expected %zu (LWC "
                  "FVector)", h[1].Size, sizeof(ue::FVector));
        GiveUp("FHitResult::Location is not an LWC FVector");
        return false;
    }
    // The normal converts the standoff from a distance along the surface normal
    // into a distance along the ray. A wrong width here would build that out of
    // whatever three doubles follow the field, so it is checked with the rest of
    // the layout rather than at the point of use.
    if (h[2].Size != sizeof(ue::FVector)) {
        Log::Line("lean-trace: FHitResult::ImpactNormal is %zu bytes, expected %zu (LWC "
                  "FVector)", h[2].Size, sizeof(ue::FVector));
        GiveUp("FHitResult::ImpactNormal is not an LWC FVector");
        return false;
    }

    // OutHit is read at OutHit + Location, so the slot has to hold a whole
    // FHitResult rather than merely start inside the frame.
    const std::size_t hitSize = ue_reflect::StructSize(hitResult);
    if (!ue_reflect::FieldFits(p[8], hitSize, paramsSize)) {
        GiveUp("SphereTraceSingle.OutHit cannot hold a whole FHitResult");
        return false;
    }

    g_layout.ParamsSize     = paramsSize;
    g_layout.WorldContext   = p[0].Offset;
    g_layout.Start          = p[1].Offset;
    g_layout.End            = p[2].Offset;
    g_layout.Radius         = p[3].Offset;
    g_layout.TraceChannel   = p[4].Offset;
    g_layout.TraceComplex   = p[5].Offset;
    g_layout.ActorsToIgnore = p[6].Offset;
    g_layout.DrawDebugType  = p[7].Offset;
    g_layout.OutHit         = p[8].Offset;
    g_layout.IgnoreSelf       = p[9].Offset;
    g_layout.BlockingHit  = h[0].Offset;
    g_layout.HitLocation  = h[1].Offset;
    g_layout.ImpactNormal = h[2].Offset;

    Log::Line("lean-trace: SphereTraceSingle frame=%zu Start=+0x%zx End=+0x%zx "
              "Radius=+0x%zx OutHit=+0x%zx | FHitResult bBlockingHit=+0x%zx "
              "Location=+0x%zx ImpactNormal=+0x%zx | standoff=%.1fcm channel=%d",
        g_layout.ParamsSize, g_layout.Start, g_layout.End, g_layout.Radius,
        g_layout.OutHit, g_layout.BlockingHit, g_layout.HitLocation,
        g_layout.ImpactNormal, g_radius, g_channel);
    g_ready = true;
    return true;
}

// One dispatch of the engine's trace. Radius 0 is how a ray is asked for without
// resolving a second UFunction: UWorld::SweepSingleByChannel routes a
// nearly-zero shape to LineTraceSingleByChannel.
struct SweepHit {
    bool ok = false;            // the dispatch itself ran
    bool blocked = false;
    ue::FVector location{};
    ue::FVector normal{};
};

SweepHit Sweep(const Vec3& start, const Vec3& direction, float distance, float radius) {
    SweepHit hit;

    // alignas: ProcessEvent hands this frame to engine code that reads its
    // fields at their natural alignment, and the frame carries LWC doubles.
    alignas(16) unsigned char buf[kMaxParams];
    std::memset(buf, 0, g_layout.ParamsSize);

    // bIgnoreSelf makes the trace exclude the actor it is handed as the world
    // context, which is why the PAWN goes here rather than the controller: the
    // ray starts at the eye, inside the player's own capsule, and without this
    // every trace would stop on the player's own body a centimetre out.
    *reinterpret_cast<std::uintptr_t*>(buf + g_layout.WorldContext) = g_pawn;

    const ue::FVector from{start.x, start.y, start.z};
    const ue::FVector to{start.x + direction.x * distance,
                         start.y + direction.y * distance,
                         start.z + direction.z * distance};
    std::memcpy(buf + g_layout.Start, &from, sizeof(from));
    std::memcpy(buf + g_layout.End, &to, sizeof(to));
    std::memcpy(buf + g_layout.Radius, &radius, sizeof(radius));
    buf[g_layout.TraceChannel] = static_cast<unsigned char>(g_channel);
    buf[g_layout.TraceComplex] = 0;
    buf[g_layout.DrawDebugType] = 0;
    buf[g_layout.IgnoreSelf] = 1;

    // An empty TArray satisfies ActorsToIgnore: the sweep only reads it, and
    // bIgnoreSelf above already excludes the one actor that matters.
    const TArrayHeader empty{nullptr, 0, 0};
    std::memcpy(buf + g_layout.ActorsToIgnore, &empty, sizeof(empty));

    if (!ue_vm::Dispatch(reinterpret_cast<void*>(g_kismetSystemCdo),
                         reinterpret_cast<void*>(g_sphereTraceFn), buf))
        return hit;

    // The return value is ignored on purpose - bBlockingHit in the struct is the
    // same answer and reading it keeps this independent of how the shipping
    // build passes a bool back.
    // bBlockingHit is the FIRST bit of its byte, so a bare bit-0 test is right
    // for it. It is also the only bitfield in FHitResult this reads, and that is
    // deliberate: UE packs bBlockingHit and bStartPenetrating into one byte, so
    // FProperty::Offset_Internal returns the SAME offset for both and only an
    // FBoolProperty ByteMask separates them. ue_reflect cannot read that mask,
    // so any second bool here would silently alias this one.
    hit.ok = true;
    hit.blocked = (buf[g_layout.OutHit + g_layout.BlockingHit] & 1u) != 0;
    std::memcpy(&hit.location, buf + g_layout.OutHit + g_layout.HitLocation,
                sizeof(hit.location));
    std::memcpy(&hit.normal, buf + g_layout.OutHit + g_layout.ImpactNormal,
                sizeof(hit.normal));
    return hit;
}

float DistanceFrom(const Vec3& start, const ue::FVector& p) {
    const double dx = p.X - start.x;
    const double dy = p.Y - start.y;
    const double dz = p.Z - start.z;
    return static_cast<float>(std::sqrt(dx * dx + dy * dy + dz * dz));
}

}  // namespace

void SetRadius(float centimetres) { g_radius = centimetres; }
void SetChannel(int traceTypeQuery) { g_channel = traceTypeQuery; }
void SetPawn(std::uintptr_t pawn) { g_pawn = pawn; }

bool Ready() { return Resolve(); }

LeanObstruction Query(void*, const Vec3& start, const Vec3& direction, float maxDistance) {
    LeanObstruction out;
    if (!Resolve() || g_pawn == 0) return out;

    // A ZERO-EXTENT trace, and the standoff is carried here rather than by the
    // shape. A swept sphere would give the standoff for free - its hit Location
    // is already backed off the surface by the radius - but it reports an
    // initial overlap whenever the eye starts within the radius of anything,
    // which in first person is routine (a doorframe, a lintel, a table edge).
    // UE reports that as a blocking hit at zero distance, which is
    // indistinguishable from a genuine contact, and taking it at face value
    // returns "no room" for a lean in ANY direction including straight away
    // from what is touching. bIgnoreSelf does not help: it excludes the pawn,
    // not the level.
    //
    // The overlap could be told apart by bStartPenetrating, but not here: UE
    // packs that bit into the same byte as bBlockingHit, so both resolve to one
    // FProperty offset and only an FBoolProperty ByteMask separates them - a
    // number this mod's reflection cannot read. So the ambiguity is removed
    // instead of decided. A ray cannot start penetrating.
    const float standoff = g_radius;

    // The ray must OVERREACH the lean, or it cannot see the surface the lean is
    // about to come to rest against: it would travel the whole way, arrive on
    // the wall, and only pull back once the head pushed far enough for the ray
    // itself to cross it.
    const float reach = maxDistance + standoff / kMinApproachCos;
    const SweepHit line = Sweep(start, direction, reach, 0.0f);
    if (!line.ok) return out;   // queried stays false: the clamp passes the lean through

    out.queried = true;
    if (!line.blocked) return out;   // nothing within the overreach: the lean is clear

    // The standoff is a distance along the SURFACE NORMAL, so the distance to
    // give back along the ray is that standoff over the cosine between them.
    // Floored, because the quotient runs away at a grazing approach.
    const double dot = direction.x * line.normal.X
                     + direction.y * line.normal.Y
                     + direction.z * line.normal.Z;
    float approach = static_cast<float>(std::fabs(dot));
    if (!(approach > kMinApproachCos)) approach = kMinApproachCos;

    const float hitDistance = DistanceFrom(start, line.location);
    const float back = standoff / approach;
    out.blocked = true;
    out.distance = hitDistance > back ? hitDistance - back : 0.0f;
    return out;
}

}  // namespace subliminal_ht::lean_trace
