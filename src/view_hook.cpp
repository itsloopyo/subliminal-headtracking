// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

// Inject the head pose into the render path, and nowhere else.
//
// GetPlayerViewPoint fires from several call sites per frame. Only the one the
// caller gate names - the FMinimalViewInfo builder inside
// ULocalPlayer::GetViewPoint - gets the pose written back. Every other caller
// (the interaction trace, the audio listener, AI perception, replication) reads
// the clean mouse/pad rotation, and that per-caller gate IS the look/aim
// decoupling: no separate save/restore sandwich is needed because the game
// simply never observes the delta.
//
// Injecting into the view info the renderer is about to build its frustum from
// is also what keeps the frame whole at extreme head angles: the tracked
// rotation is in the struct before the projection matrix, the culling and the
// streaming queries are derived from it, so everything the player can see is
// something the renderer was asked to draw.

#include "view_hook.h"

#include <atomic>
#include <cmath>
#include <cstdint>
#include <mutex>
#include <tuple>
#include <unordered_map>

#include <windows.h>
#include <intrin.h>

#include "aim_projection.h"
#include "aim_trace.h"
#include "builds/build_registry.h"
#include "camera_boundary.h"
#include "camera_fov.h"
#include "flashlight.h"
#include "game_state.h"
#include "inject_mode.h"
#include "lean_trace.h"
#include "logging.h"
#include "reticle_mover.h"
#include "ue_objects.h"
#include "widget_probe.h"

#include "cameraunlock/camera/lean_clamp.h"
#include "cameraunlock/effects/head_follow_light.h"
#include "cameraunlock/data/tracking_pose.h"
#include "cameraunlock/camera/zoom_compensation.h"
#include "cameraunlock/hooks/hook_manager.h"
#include "cameraunlock/math/vec3.h"
#include "cameraunlock/time/frame_clock.h"
#include "cameraunlock/unreal/ue_math.h"
#include "cameraunlock/unreal/ue_runtime.h"

namespace subliminal_ht::view_hook {

namespace {

namespace ue = ::cameraunlock::unreal;

using ue::FQuat4d;
using ue::FRotator;
using ue::FVector;
using cameraunlock::time::FrameClock;

static_assert(std::tuple_size<decltype(OffsetTable::kKnownCallerRvas)>::value
                  == inject::kCallerSlots,
              "inject::kCallerSlots must match the profile's caller RVA table");

// APlayerController::GetPlayerViewPoint(self, &OutLocation, &OutRotation).
// FVector/FRotator are FVector3d/FRotator3d (24 bytes) under UE5 LWC.
using GetPlayerViewPoint_t = void(__fastcall*)(void* self, FVector* outLocation,
                                               FRotator* outRotation);

Dependencies g_deps{};

std::atomic<bool> g_trackingEnabled{true};
std::atomic<bool> g_worldSpaceYaw{true};
std::atomic<int>  g_injectMode{inject::kFirstCaller};

GetPlayerViewPoint_t g_origGetPlayerViewPoint = nullptr;
std::atomic<std::uint64_t> g_hookCallCount{0};

// Ticked only by the injected render-path caller, so the session and the lean
// clamp both see one dt per rendered frame.
FrameClock g_frameClock;

// The lean allowance. Stateful, so it is reset whenever no lean is applied at
// all - otherwise the wall the player has already walked away from keeps
// rationing the next room's lean through its release ease.
cameraunlock::camera::LeanClamp g_leanClamp;

// ---- diagnostics cadence -------------------------------------------------
constexpr std::uint64_t kHeartbeatMs = 30000;
// The pose detail is bounded on top of its interval: the composition evidence
// (clean vs tracker vs result) is all there in the first 40s, and left running
// it costs a few hundred KB an hour and buries the startup chain a user is
// asked to read. The heartbeat carries liveness thereafter.
constexpr std::uint64_t kPoseDetailMs    = 2000;
constexpr int           kPoseDetailLines = 20;
constexpr std::uint64_t kCallerSummaryEvery = 1800;
constexpr std::uint64_t kWidgetProbeMs     = 10000;
constexpr int           kWidgetProbePasses = 8;
// The clamp's own state, on a transition and then periodically. Transitions
// alone cannot tell "the sweep runs and the room is open" from "the sweep is
// not running", and those need different fixes.
constexpr std::uint64_t kLeanStateMs = 15000;

// ---- caller gate ---------------------------------------------------------
bool ShouldInjectForCaller(std::uintptr_t retRva, int mode) {
    return inject::ShouldInject(retRva, mode, Offsets().kKnownCallerRvas);
}

// Whether ANY caller can be injected for in this mode.
//
// False for kNone, and for every single-caller mode whose profile slot is still
// 0 - which is all of 2..16 on the current build. The distinction matters
// because "this caller is not the render path" and "no caller is" need opposite
// treatment: the first is a plain return, since the render caller will do the
// work later in the same frame, while the second has to stand down or the
// reticle stays parked wherever the last tracked frame left it and the lean
// clamp carries that room's wall forward.
bool AnyCallerInjects(int mode) {
    return mode == inject::kAllCallers
        || inject::CallerRva(mode, Offsets().kKnownCallerRvas) != 0;
}

// Caller distribution accounting, only meaningful in the all-callers mode -
// that's how the render caller gets re-confirmed after a patch. Off the hot
// path otherwise.
std::mutex g_callerMutex;
std::unordered_map<std::uintptr_t, std::uint64_t> g_callerCounts;
std::atomic<std::uint64_t> g_callerLastSummary{0};

void DumpCallerSummary(std::uint64_t total) {
    std::lock_guard<std::mutex> lk(g_callerMutex);
    Log::Line("caller-summary @%llu calls: %zu unique return RVAs:",
        static_cast<unsigned long long>(total), g_callerCounts.size());
    for (const auto& kv : g_callerCounts)
        Log::Line("  ret RVA 0x%08llx  count=%llu",
            static_cast<unsigned long long>(kv.first),
            static_cast<unsigned long long>(kv.second));
}

void CountCaller(std::uintptr_t retRva, std::uint64_t call) {
    { std::lock_guard<std::mutex> lk(g_callerMutex); ++g_callerCounts[retRva]; }
    if (call - g_callerLastSummary.load(std::memory_order_relaxed) >= kCallerSummaryEvery) {
        g_callerLastSummary.store(call, std::memory_order_relaxed);
        DumpCallerSummary(call);
    }
}

// ---- tracker link --------------------------------------------------------
// GetRotation() latches true on the first packet and never goes back, so it
// answers "has a tracker ever connected", not "is one sending now". Staleness
// against the last receive timestamp is the live question, and the receiver
// stamps packets with the same steady_clock, which is what makes it comparable.
bool TrackerFresh() {
    const auto* receiver = g_deps.receiver;
    if (!receiver) return false;
    const std::int64_t last = receiver->GetLastReceiveTimestamp();
    if (last == 0) return false;
    return cameraunlock::TrackingPose::CurrentTimestamp() - last <
           static_cast<std::int64_t>(cameraunlock::UdpReceiver::kConnectionTimeoutMs) * 1000;
}

// Gained and lost, on the transition only. Without it nothing in the log
// carries the moment a tracker stopped sending: the heartbeat below can be
// half a minute late, and that moment is the first thing "the view froze"
// needs. Seeded not-fresh, so a session where no tracker ever connects says
// nothing here - the init line already named the port being waited on.
void LogTrackerLink() {
    static std::atomic<int> s_last{0};
    const int fresh = TrackerFresh() ? 1 : 0;
    if (s_last.exchange(fresh) == fresh) return;
    Log::Line("tracker: %s", fresh
        ? "pose data arriving - tracking is live"
        : "pose data stopped - the view holds the last pose until it resumes");
}

// ---- diagnostics ---------------------------------------------------------
void LogHeartbeat(std::uint64_t call, std::uintptr_t retRva,
                  const game_state::Verdict& gate, int mode) {
    static std::atomic<std::uint64_t> s_lastTick{0};
    const std::uint64_t now = GetTickCount64();
    if (call != 1 && (now - s_lastTick.load(std::memory_order_relaxed)) < kHeartbeatMs)
        return;
    s_lastTick.store(now, std::memory_order_relaxed);

    auto* receiver = g_deps.receiver;
    float hy = 0, hp = 0, hr = 0;
    if (receiver) receiver->GetRotation(hy, hp, hr);
    // Three states, because one "NO" covered two faults that need different
    // fixes: a tracker that has never sent anything, and one that sent and then
    // stopped. GetRotation() cannot tell them apart, so freshness decides.
    const char* udpData = !receiver || receiver->GetLastReceiveTimestamp() == 0
                              ? "none"
                              : TrackerFresh() ? "live" : "stale";
    // Port state alongside the data flag, because "udpData=NO" alone cannot
    // tell a stalled tracker from a port another game still holds - and only
    // the second one resolves itself.
    const char* udpPort = !receiver              ? "none"
                        : receiver->IsRetrying() ? "waiting-for-port"
                        : receiver->IsRunning()  ? "listening"
                                                 : "down";
    Log::Line("heartbeat hook=%llu retRVA=0x%08llx enabled=%s gameplay=%s offline=%s "
              "netKnown=%s udpPort=%s udpData=%s raw=(Y=%.2f P=%.2f R=%.2f) fov=%.1f "
              "base=%.1f zoom=%.4f yawMode=%s injectMode=%d",
        static_cast<unsigned long long>(call),
        static_cast<unsigned long long>(retRva),
        g_trackingEnabled.load() ? "ON" : "OFF",
        gate.InGameplay ? "YES" : "NO",
        gate.Offline ? "YES" : "NO",
        gate.NetStateKnown ? "YES" : "NO",
        udpPort,
        udpData, hy, hp, hr,
        camera_fov::GameFov(), camera_fov::BaseFov(), camera_fov::LastZoomFactor(),
        g_worldSpaceYaw.load() ? "world" : "local", mode);
}

// Widget discovery. Every GetPlayerViewPoint caller is on the game thread,
// which is what walking the object table requires; it deliberately does not
// wait for tracker data, since finding the widget has nothing to do with having
// a pose.
void MaybeRunWidgetProbe() {
    static std::atomic<bool> s_disabled{false};
    static std::atomic<std::uint64_t> s_lastPass{0};
    static std::atomic<int> s_passesRun{0};

    if (s_disabled.load(std::memory_order_relaxed)) return;
    if (s_passesRun.load(std::memory_order_relaxed) >= kWidgetProbePasses) return;
    const std::uint64_t now = GetTickCount64();
    if ((now - s_lastPass.load(std::memory_order_relaxed)) < kWidgetProbeMs) return;
    s_lastPass.store(now, std::memory_order_relaxed);

    const int pass = s_passesRun.fetch_add(1, std::memory_order_relaxed);
    // A profile whose UObject globals do not check out cannot be walked at all,
    // so the first failure ends the probe rather than repeating it.
    if (pass == 0 && !ue_objects::ValidateGlobals()) {
        s_disabled.store(true, std::memory_order_relaxed);
        return;
    }
    Log::Line("widget-probe: pass %d", pass);
    widget_probe::DumpCandidates();
}

struct PoseSample {
    std::uint64_t  Call;
    std::uintptr_t RetRva;
    FRotator       Clean;
    float          Yaw, Pitch, Roll;
    FRotator       Result;
    float          OffsetX, OffsetY, OffsetZ;
    FVector        PositionOffsetUE;
    double         AimDistance;
    bool           AimHit;
};

// Time-gated rather than call-gated because this fires on the render caller, so
// a call-count interval would log at the player's frame rate.
//
// Rotation, parallax, distance and lean land on ONE line on purpose: reading
// the distance from one log line and the offset from another is what makes a
// projection fault and a scaling fault look alike. The tracker columns carry the
// pose as APPLIED, zoom scaling included, so the line describes the camera the
// player is looking through rather than what arrived over the wire, and the
// factor that got it there is on the same line.
void LogPoseDetail(const PoseSample& s) {
    static std::atomic<std::uint64_t> s_lastLine{0};
    static std::atomic<int> s_linesWritten{0};
    if (s_linesWritten.load(std::memory_order_relaxed) >= kPoseDetailLines) return;
    const std::uint64_t now = GetTickCount64();
    if (s.Call != 1 && (now - s_lastLine.load(std::memory_order_relaxed)) < kPoseDetailMs)
        return;
    s_lastLine.store(now, std::memory_order_relaxed);
    s_linesWritten.fetch_add(1, std::memory_order_relaxed);

    float dx = 0.0f, dy = 0.0f;
    const bool haveOffset = aim_projection::GetScreenOffset(dx, dy);
    Log::Line("hook #%llu retRVA=0x%08llx clean=(Y=%.2f P=%.2f R=%.2f) "
              "tracker=(Y=%.2f P=%.2f R=%.2f) result=(Y=%.2f P=%.2f R=%.2f) "
              "headOff_m=(x%.3f y%.3f z%.3f) posOff_ue=(%.1f,%.1f,%.1f) "
              "aim=%s dist=%.1f reticle_px=(%.1f,%.1f)%s fov=%.2f zoom=%.4f",
        static_cast<unsigned long long>(s.Call),
        static_cast<unsigned long long>(s.RetRva),
        s.Clean.Yaw, s.Clean.Pitch, s.Clean.Roll, s.Yaw, s.Pitch, s.Roll,
        s.Result.Yaw, s.Result.Pitch, s.Result.Roll,
        s.OffsetX, s.OffsetY, s.OffsetZ,
        s.PositionOffsetUE.X, s.PositionOffsetUE.Y, s.PositionOffsetUE.Z,
        s.AimHit ? "hit" : "miss", s.AimDistance,
        dx, dy, haveOffset ? "" : " (invalid)", camera_fov::GameFov(),
        camera_fov::LastZoomFactor());
}

// `queryInstalled` is false on any frame the clamp was handed no query at all -
// collision switched off, the pawn not possessed yet, the trace not resolved
// yet. That case needs a state of its own: core raises LastQueryFailed only when
// a query it HAS ran and could not answer, so without this a frame with no query
// reads exactly like a room with no walls in it. The lean is completely
// unclamped in both.
void LogLeanState(bool queryInstalled) {
    static std::atomic<int> s_lastState{-1};
    static std::atomic<std::uint64_t> s_lastLine{0};
    const int state = !queryInstalled ? 3
                    : g_leanClamp.LastQueryFailed() ? 2
                    : g_leanClamp.InContact() ? 1
                                              : 0;
    const std::uint64_t now = GetTickCount64();
    const bool changed = s_lastState.exchange(state) != state;
    if (!changed && now - s_lastLine.load(std::memory_order_relaxed) < kLeanStateMs) return;
    s_lastLine.store(now, std::memory_order_relaxed);
    Log::Line("lean-clamp: %s",
        state == 3 ? "no collision query this frame - the lean is unclamped and can "
                     "put the view inside a wall"
      : state == 2 ? "the trace is NOT running - the lean is unclamped and can put "
                     "the view inside a wall"
      : state == 1 ? "holding the view off a surface"
                   : "clear, the room takes the whole lean");
}

std::uintptr_t ReturnRva(const void* returnAddress) {
    const auto addr = reinterpret_cast<std::uintptr_t>(returnAddress);
    return ue::ModuleBase() != 0 ? addr - ue::ModuleBase() : addr;
}

// ---- the frame's field of view -------------------------------------------
struct FovFrame {
    float Degrees = 0.0f;
    float Zoom = 1.0f;
};

// Read the field of view this frame will be drawn with, and derive the factor
// that keeps a zoom from changing how far the head moves the picture.
//
// The FOV sits a four-byte load past the Location pointer, and the stride check
// inside camera_fov::Read is what proves the two out-params really are fields of
// one FMinimalViewInfo. Only the gated render caller may ask: in the all-callers
// mode that check is not licence enough, because an unrecognised caller's stack
// frame can satisfy it by coincidence.
//
// Read, never written: the reticle has to be projected through the field of view
// the frame is actually drawn with, and the pose has to be scaled against it.
// That is the only reason the mod looks at it.
//
// Derived here rather than down in the tracked path so the basis line it logs
// lands on the first frame the CAMERA updates rather than the first frame a pose
// arrives. A factor that is wrong by a constant reads exactly like a factor that
// is right, so the terms have to be checkable with no tracker connected and no
// save loaded.
FovFrame ResolveFrameFov(std::uintptr_t controller, const FVector* outLocation,
                         const FRotator* outRotation) {
    FovFrame frame;
    frame.Degrees = camera_fov::Read(outLocation, outRotation);
    camera_fov::ResolveAspectConstraint(controller);
    camera_fov::ResolveBaseFov();
    frame.Zoom = camera_fov::ZoomFactor(frame.Degrees);
    return frame;
}

// ---- standing down --------------------------------------------------------
// Standing down, from any of the reasons there are for it: the gate shut,
// tracking switched off, or the tracker stopped sending. All three leave the
// camera clean, so all three have to put the reticle back with it - a mark left
// holding its last tracked offset over an untracked view reads as the reticle
// having come loose rather than as tracking having stopped.
//
// Latched so the widget is pushed exactly once on the way out rather than on
// every frame for as long as the menu is up or the tracker is quiet.
std::atomic<bool> g_standDownLatched{false};

void StandDown() {
    aim_projection::SetActive(false);
    flashlight::Release();
    // A camera that is not being leaned has no allowance to carry forward, and
    // a menu or a level load is exactly the cut the clamp must forget.
    g_leanClamp.Reset();
    if (!g_standDownLatched.exchange(true, std::memory_order_relaxed))
        reticle_mover::Tick();
}

// ---- the tracked path -----------------------------------------------------
struct LeanSample {
    // The world-space offset actually added to the eye, after the collision
    // clamp cut it to whatever the room leaves.
    FVector Applied{0.0, 0.0, 0.0};
    // The pose offset the clamp was asked for, in metres, zoom scaling included.
    float X = 0.0f, Y = 0.0f, Z = 0.0f;
};

// Move the eye by the tracked head position, in the CLEAN camera frame so the
// sway follows the body rather than the head-rotated view, then cut it to
// whatever the room leaves. Adds the surviving offset to outLocation.
LeanSample ApplyLean(Session& session, const Config& config, const FQuat4d& cleanQ,
                     const FVector& cleanEye, std::uintptr_t pawn, float dt,
                     float zoom, FVector* outLocation) {
    LeanSample lean;
    if (!session.GetPositionOffset(lean.X, lean.Y, lean.Z)) {
        g_leanClamp.Reset();
        return lean;
    }

    // The lean scales linearly rather than through a tangent: a head offset d
    // seen at depth D lands at d / (2 * D * tan(fov/2)) of the frame, so holding
    // the screen displacement fixed is a straight multiply.
    lean.X *= zoom;
    lean.Y *= zoom;
    lean.Z *= zoom;
    const FVector desiredUE =
        camera_boundary::PositionOffset(cleanQ, lean.X, lean.Y, lean.Z);

    lean_trace::SetPawn(pawn);
    const bool sweep = config.collision_enabled && pawn != 0 && lean_trace::Ready();
    const cameraunlock::math::Vec3 desired{
        static_cast<float>(desiredUE.X),
        static_cast<float>(desiredUE.Y),
        static_cast<float>(desiredUE.Z)};
    // The sweep starts from the CLEAN eye - the position the game itself put the
    // camera at. Clamping after the offset is added would be reading back a
    // position that is already inside the wall.
    const cameraunlock::math::Vec3 eye{
        static_cast<float>(cleanEye.X),
        static_cast<float>(cleanEye.Y),
        static_cast<float>(cleanEye.Z)};
    const cameraunlock::math::Vec3 clamped = g_leanClamp.Apply(
        eye, desired, dt, sweep ? &lean_trace::Query : nullptr, nullptr);
    lean.Applied = FVector{clamped.x, clamped.y, clamped.z};
    LogLeanState(sweep);

    outLocation->X += lean.Applied.X;
    outLocation->Y += lean.Applied.Y;
    outLocation->Z += lean.Applied.Z;
    return lean;
}

struct AimSample {
    double Distance = 0.0;
    bool   Hit = false;
};

// Where the interaction ray lands, projected into the frame that is about to be
// drawn, and pushed onto the game's own crosshair.
//
// The ray leaves the CLEAN eye along the CLEAN forward - it is the mouse-driven
// aim the game will use - while the frame is drawn from the leaned eye, and the
// vector between the two is the parallax the reticle has to carry.
AimSample UpdateReticle(const Config& config, std::uintptr_t pawn,
                        const FVector& renderedEye, const FVector& cleanEye,
                        const FQuat4d& cleanQ, const FQuat4d& trackedQ,
                        float fovDegrees) {
    AimSample aim;

    aim_projection::Frame frame;
    frame.RenderedEye = renderedEye;
    frame.TrackedRotation = trackedQ;
    frame.Direction = ue::QuatRotateVec(cleanQ, FVector{1.0, 0.0, 0.0});
    if (config.reticle_follows_aim && pawn != 0) {
        const aim_trace::Result hit = aim_trace::Cast(
            pawn, cleanEye, frame.Direction, config.aim_trace_distance);
        if (hit.Valid && hit.Hit) {
            frame.Point = hit.Point;
            frame.HasPoint = true;
            aim.Distance = hit.Distance;
            aim.Hit = true;
        }
        // A definite no-hit leaves HasPoint false, and the projection then uses
        // the aim direction - which is what a target at infinity projects to.
        // A cast that could not run leaves it false too, and the projection is
        // then the honest rotation-only one rather than a guessed depth.
    }
    aim_projection::Update(frame, fovDegrees, config.reticle_follows_aim);
    reticle_mover::Tick();
    return aim;
}

void __fastcall GetPlayerViewPoint_Hook(void* self, FVector* outLocation,
                                        FRotator* outRotation) {
    const std::uintptr_t retRva = ReturnRva(_ReturnAddress());
    const auto controller = reinterpret_cast<std::uintptr_t>(self);

    const game_state::Verdict gate = game_state::Evaluate(controller);
    game_state::LogTransitions(gate);

    g_origGetPlayerViewPoint(self, outLocation, outRotation);
    const FRotator clean = *outRotation;
    const FVector  cleanEye = *outLocation;

    const auto call = g_hookCallCount.fetch_add(1, std::memory_order_relaxed) + 1;
    const int mode = g_injectMode.load(std::memory_order_relaxed);
    if (mode == inject::kAllCallers)
        CountCaller(retRva, call);

    const Config& config = *g_deps.config;

    const bool renderCaller = ShouldInjectForCaller(retRva, mode);
    const FovFrame fov = renderCaller && mode != inject::kAllCallers
        ? ResolveFrameFov(controller, outLocation, outRotation)
        : FovFrame{};

    LogTrackerLink();
    LogHeartbeat(call, retRva, gate, mode);

    if (config.widget_dump && gate.InGameplay)
        MaybeRunWidgetProbe();

    const bool allowed = g_trackingEnabled.load(std::memory_order_relaxed)
                      && gate.InGameplay
                      && gate.Offline
                      && AnyCallerInjects(mode);
    if (!allowed) {
        StandDown();
        return;
    }

    // Decoupling: only the render-path caller gets the head pose written back.
    // Every other GetPlayerViewPoint caller keeps the clean mouse/pad rotation.
    // A plain return rather than a stand-down: the render caller runs later in
    // the same frame and owns the reticle and the lean. AnyCallerInjects above
    // is what covers the modes where it never will.
    if (!renderCaller) return;

    Session& session = *g_deps.session;
    const float dt = g_frameClock.Tick();
    float yaw = 0.0f, pitch = 0.0f, roll = 0.0f;
    if (!session.Update(dt) || !session.GetRotation(yaw, pitch, roll)) {
        StandDown();
        return;
    }
    g_standDownLatched.store(false, std::memory_order_relaxed);

    // A zoom must not change how far the head moves the view. Subliminal's zoom
    // input winds the player camera's focal length up from 49mm to about 145mm,
    // narrowing the field of view from 91.2 to 39.3 degrees, and a narrow field
    // magnifies everything in the frame - head tracking included. Scaling the
    // pose here, before it reaches the camera, is what makes the picture move
    // the same distance zoomed or not.
    //
    // Yaw, pitch and the lean all translate the image, so all three scale. Roll
    // does not: ten degrees of head roll rolls the picture ten degrees at every
    // field of view there is, and scaling it would flatten a tilt the player is
    // holding. The factor is exactly 1.0 whenever nothing is zoomed, so ordinary
    // play is untouched.
    //
    // Only within +/-90 degrees, which is ScaleAngleForZoom's stated contract.
    // It is atan(tan(a) * factor), so past a quarter turn tan changes sign and
    // atan folds the answer back into (-90, 90): the head turns 100 degrees one
    // way and the view swings 63 the other. Nothing upstream bounds the pose -
    // there is no rotation limit in this mod, and YawSensitivity alone reaches
    // 100 - so the bound belongs here. Passing an out-of-contract angle through
    // unscaled is the honest answer: it is the uncompensated view, not a
    // wrong-signed one.
    constexpr float kMaxScalableDeg = 90.0f;
    if (fov.Zoom != 1.0f) {
        if (std::fabs(yaw)   < kMaxScalableDeg)
            yaw   = cameraunlock::camera::ScaleAngleForZoom(yaw, fov.Zoom);
        if (std::fabs(pitch) < kMaxScalableDeg)
            pitch = cameraunlock::camera::ScaleAngleForZoom(pitch, fov.Zoom);
    }

    const bool worldSpaceYaw = g_worldSpaceYaw.load(std::memory_order_relaxed);
    const FQuat4d cleanQ = ue::QuatFromEulerDeg(clean.Pitch, clean.Yaw, clean.Roll);
    camera_boundary::ApplyHeadPose(*outRotation, yaw, pitch, roll, worldSpaceYaw);
    const FQuat4d trackedQ = ue::QuatFromEulerDeg(
        outRotation->Pitch, outRotation->Yaw, outRotation->Roll);

    const std::uintptr_t pawn = game_state::PossessedPawn(controller);
    const LeanSample lean = ApplyLean(session, config, cleanQ, cleanEye, pawn, dt,
                                      fov.Zoom, outLocation);
    const AimSample aim = UpdateReticle(config, pawn, *outLocation, cleanEye, cleanQ,
                                        trackedQ, fov.Degrees);

    // After the lean, because the beam leaves the eye the frame is drawn from,
    // and that is not settled until the collision clamp has had the offset.
    //
    // The beam leads the head rather than matching it, so it gets a composition
    // of its own: the SAME ApplyHeadPose the camera just went through, handed
    // the same pose multiplied. Scaling the pose and re-composing, rather than
    // scaling the delta the camera produced, is what core's head_follow_light.h
    // asks for - the two are not the same operation once more than one axis is
    // non-zero, and matching the camera's composition is what stops the beam and
    // the view disagreeing about which way the head turned.
    if (config.flashlight.follows_head) {
        const auto beam = cameraunlock::effects::ScaleHeadEuler(
            {static_cast<float>(yaw), static_cast<float>(pitch), static_cast<float>(roll)},
            config.flashlight.multiplier);
        FRotator beamRot = clean;
        camera_boundary::ApplyHeadPose(beamRot, beam.yaw, beam.pitch, beam.roll,
                                       worldSpaceYaw);
        flashlight::Follow(pawn, cleanQ,
                           ue::QuatFromEulerDeg(beamRot.Pitch, beamRot.Yaw, beamRot.Roll),
                           lean.Applied);
    }

    LogPoseDetail({call, retRva, clean, yaw, pitch, roll, *outRotation,
                   lean.X, lean.Y, lean.Z, lean.Applied, aim.Distance, aim.Hit});
}

}  // namespace

bool Install(const Dependencies& deps) {
    g_deps = deps;
    g_trackingEnabled.store(deps.config->enable_on_startup);
    g_worldSpaceYaw.store(deps.config->world_space_yaw);
    g_injectMode.store(Offsets().kDefaultInjectMode);

    cameraunlock::camera::LeanClampSettings lean;
    // Zero, because lean_trace applies the standoff itself: it traces a ray and
    // subtracts the standoff along the surface normal before answering, so the
    // distance it reports is already the travel the room allows. A skin on top
    // would hold the eye off twice - a configured 20cm parked the view 40cm out.
    lean.skin = 0.0f;
    lean.release_smoothing = deps.config->collision_release_smoothing;
    g_leanClamp.SetSettings(lean);
    lean_trace::SetRadius(deps.config->collision_radius);
    lean_trace::SetChannel(deps.config->collision_channel);
    aim_trace::SetTraceChannel(deps.config->aim_trace_channel);

    auto& hm = cameraunlock::hooks::HookManager::Instance();
    void* target = reinterpret_cast<void*>(
        ue::ModuleBase() + Offsets().kGetPlayerViewPointRva);
    if (auto s = hm.CreateHook(target, reinterpret_cast<void*>(&GetPlayerViewPoint_Hook),
                               reinterpret_cast<void**>(&g_origGetPlayerViewPoint));
        s != cameraunlock::hooks::HookStatus::Ok) {
        Log::Line("FATAL: CreateHook(GetPlayerViewPoint) failed: %s",
            cameraunlock::hooks::HookStatusToString(s));
        return false;
    }
    if (auto s = hm.EnableHook(target); s != cameraunlock::hooks::HookStatus::Ok) {
        Log::Line("FATAL: EnableHook failed: %s", cameraunlock::hooks::HookStatusToString(s));
        return false;
    }
    Log::Line("GetPlayerViewPoint hooked at RVA 0x%08llx (default inject mode %d)",
        static_cast<unsigned long long>(Offsets().kGetPlayerViewPointRva),
        g_injectMode.load());
    return true;
}

bool TrackingEnabled() { return g_trackingEnabled.load(); }
void SetTrackingEnabled(bool enabled) { g_trackingEnabled.store(enabled); }

bool WorldSpaceYaw() { return g_worldSpaceYaw.load(); }
void SetWorldSpaceYaw(bool worldSpaceYaw) { g_worldSpaceYaw.store(worldSpaceYaw); }

int  InjectMode() { return g_injectMode.load(); }
void SetInjectMode(int mode) { g_injectMode.store(mode); }

}  // namespace subliminal_ht::view_hook
