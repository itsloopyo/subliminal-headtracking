// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "camera_fov.h"

#include <atomic>
#include <cmath>

#include <windows.h>

#include "builds/build_registry.h"
#include "logging.h"
#include "ue_objects.h"
#include "ue_reflect.h"
#include "ue_vm.h"

#include "cameraunlock/camera/zoom_compensation.h"

#include "cameraunlock/unreal/ue_runtime.h"

// Where the field of view comes from.
//
// ULocalPlayer::GetViewPoint - the render caller the view hook's caller gate
// injects for - fills the FMinimalViewInfo it was handed like this:
//
//     OutViewInfo      = CameraManager->GetCameraCacheView();
//     OutViewInfo.FOV  = CameraManager->GetFOVAngle();
//     PC->GetPlayerViewPoint(&OutViewInfo.Location,     <- this mod's hook
//                            &OutViewInfo.Rotation);
//
// So by the time the hook runs, the FOV this frame will be drawn with is
// already in the struct, one FVector past the Location pointer the hook was
// handed. Reading it there is what keeps the reticle projection on the same
// field of view the frame is rendered at, including whatever the game does to
// it of its own accord.
namespace subliminal_ht::camera_fov {

namespace {

namespace ue = ::cameraunlock::unreal;

std::atomic<float> g_gameFov{0.0f};
std::atomic<float> g_baseFov{0.0f};
std::atomic<float> g_lastZoom{1.0f};
std::atomic<int>   g_constraint{kConstraintUnknown};

// The player camera's archetype - the class-default subobject the pawn's live
// CineCameraComponent is built from. Its FieldOfView is the authored, un-zoomed
// value, and unlike the live component's it does not move when the zoom input
// winds the focal length up.
constexpr const char* kCameraArchetypeName  = "CineCamera_GEN_VARIABLE";
constexpr const char* kCameraArchetypeOuter = "SubliminalCharacter_C";

ue_vm::ResolveRetry g_baseFovRetry;
bool g_baseFovFailed = false;

bool g_constraintFailed = false;
std::uint64_t g_constraintReadMs = 0;

// How often the constraint is re-read once it has been read successfully.
//
// It is NOT latched, because the game gives the player a control for it: the
// settings menu carries an FOVCon switch whose two positions are LockX and
// LockY, and flipping it mid-session changes which axis the field of view
// describes. A cached answer would then have the reticle projecting through the
// wrong axis model - on a 32:9 display that is a factor of 3.5 horizontally,
// which is the whole reason the constraint is read from the engine rather than
// assumed. The read is two guarded loads off a cached property offset, so a
// second between them costs nothing and bounds how long a flip can be wrong.
constexpr std::uint64_t kConstraintRereadMs = 1000;

// Said once, then never again. Which axis the engine holds the field of view on
// decides the whole projection, so without it the reticle pass draws nothing
// rather than moving the crosshair to a place the player is not pointing.
void GiveUpOnConstraint(const char* what) {
    if (g_constraintFailed) return;
    g_constraintFailed = true;
    Log::Line("fov: %s. Which axis the engine holds the field of view on cannot be "
              "read, so the reticle will stay where the game draws it.", what);
}

// APlayerController::Player, the UPlayer this controller belongs to. For the
// controller the render caller asks for a view point, that is the ULocalPlayer
// holding the viewport - which is the object that owns the aspect constraint.
//
// 0 means "no answer this frame", which covers both the early frames that run
// before the player is attached and a give-up: GiveUpOnConstraint latches, so
// the caller stops asking on its own.
std::uintptr_t ResolveLocalPlayer(std::uintptr_t controller) {
    const std::uintptr_t controllerClass = ue_objects::ClassOf(controller);
    if (!controllerClass) return 0;

    // Held against the class it was resolved on. FindPropertyInChain climbs the
    // whole SuperStruct chain and builds two heap strings for every field it
    // walks past, and the frames before the player is attached run this on every
    // one of them - hundreds of allocations a frame for a number that does not
    // move. Keyed by the class rather than latched, so a controller of a
    // different class resolves its own offset.
    static std::uintptr_t s_class = 0;
    static std::size_t s_playerOffset = 0;
    if (controllerClass != s_class) {
        ue_reflect::FieldInfo player;
        if (!ue_reflect::FindPropertyInChain(controllerClass, "Player", player)) {
            GiveUpOnConstraint("APlayerController::Player is not in the controller's "
                               "property chain");
            return 0;
        }
        if (player.TypeName != "ObjectProperty" || player.Size != sizeof(std::uintptr_t)) {
            GiveUpOnConstraint("APlayerController::Player is not a pointer-sized object "
                               "property");
            return 0;
        }
        s_playerOffset = player.Offset;
        s_class = controllerClass;
    }

    std::uintptr_t localPlayer = 0;
    if (!ue::SafeReadPtr(controller + s_playerOffset, localPlayer)) return 0;
    return localPlayer;
}

}  // namespace

float Read(const void* outLocation, const void* outRotation) {
    const auto& mvi = Offsets().MinimalViewInfoLayout;
    const auto locAddr = reinterpret_cast<std::uintptr_t>(outLocation);
    const auto rotAddr = reinterpret_cast<std::uintptr_t>(outRotation);
    // Unless the two pointers are exactly one FVector apart they are not fields
    // of one view info, and reading past the first would land in some other
    // caller's stack frame.
    if (rotAddr - locAddr != mvi.kRotationStride) return 0.0f;

    float fov = 0.0f;
    if (!ue::SafeReadFloat(locAddr + mvi.kFovOffset, fov)) return 0.0f;
    if (!Plausible(fov)) return 0.0f;
    g_gameFov.store(fov, std::memory_order_relaxed);

    static std::atomic<bool> s_announced{false};
    if (!s_announced.exchange(true, std::memory_order_relaxed))
        Log::Line("fov: the game renders at %.1f degrees", fov);
    return fov;
}

void ResolveAspectConstraint(std::uintptr_t controller) {
    if (g_constraintFailed) return;
    const std::uint64_t now = GetTickCount64();
    if (g_constraintReadMs != 0 && now - g_constraintReadMs < kConstraintRereadMs) return;

    // Early frames run before the player is attached; the next frame reads it.
    const std::uintptr_t localPlayer = ResolveLocalPlayer(controller);
    if (!localPlayer) return;

    const std::uintptr_t playerClass = ue_objects::ClassOf(localPlayer);
    if (!playerClass) return;

    // Held against the player class for the same reason ResolveLocalPlayer holds
    // its own: this re-reads on an interval for the whole session, and the
    // property walk behind it is what the header calls "two guarded loads off a
    // cached property offset".
    static std::uintptr_t s_playerClass = 0;
    static std::size_t s_constraintOffset = 0;
    if (playerClass != s_playerClass) {
        ue_reflect::FieldInfo constraint;
        if (!ue_reflect::FindPropertyInChain(playerClass, "AspectRatioAxisConstraint",
                                             constraint)) {
            ue_reflect::DumpProperties("UPlayer", playerClass);
            GiveUpOnConstraint("AspectRatioAxisConstraint is not in the player's property "
                               "chain - the table above is what it does carry");
            return;
        }
        // TEnumAsByte<EAspectRatioAxisConstraint>. A wider field here is a
        // different property than the one this is reading, and taking its low
        // byte would be a coin toss between two projections.
        if (constraint.Size != 1) {
            GiveUpOnConstraint("AspectRatioAxisConstraint is not one byte wide");
            return;
        }
        s_constraintOffset = constraint.Offset;
        s_playerClass = playerClass;
    }

    // Core has no byte-wide guarded read; the second byte this picks up is
    // discarded.
    std::uint16_t word = 0;
    if (!ue::SafeReadU16(localPlayer + s_constraintOffset, word)) return;

    const int value = static_cast<int>(word & 0xFFu);
    if (value != kMaintainYFOV && value != kMaintainXFOV && value != kMajorAxisFOV) {
        GiveUpOnConstraint("AspectRatioAxisConstraint holds a value that is not one of "
                           "the three the enum defines, so it is not where the engine's "
                           "reflection data says it is");
        return;
    }

    g_constraintReadMs = now;
    const int previous = g_constraint.exchange(value, std::memory_order_relaxed);
    if (previous == value) return;
    // The numeric value as well as the name: a later engine version that
    // renumbers the enum would keep printing a name from this mod's own table
    // while meaning something else, and the number is what makes that visible.
    Log::Line("fov: AspectRatioAxisConstraint=%d (%s), so on this viewport the field "
              "of view scalar is the %s field",
              value,
              value == kMaintainYFOV   ? "MaintainYFOV"
              : value == kMaintainXFOV ? "MaintainXFOV"
                                       : "MajorAxisFOV",
              value == kMaintainYFOV ? "vertical" : "horizontal");
}

void ResolveBaseFov() {
    if (g_baseFov.load(std::memory_order_relaxed) > 0.0f || g_baseFovFailed) return;
    // The attempt budget lives in ResolveRetry, not here. A second counter would
    // never be reached: once Due() is spent it returns false for the rest of the
    // session, so this function would stop running before its own limit fired
    // and the line below - the only thing that says the zoom compensation is
    // off - would never print. A walk costs about 5ms and the archetype exists
    // from engine init, so a budget that runs out means it is not there.
    if (!g_baseFovRetry.Due()) {
        if (g_baseFovRetry.Exhausted()) {
            g_baseFovFailed = true;
            Log::Line("fov: the player camera archetype %s under %s was not found, so the "
                      "game's un-zoomed field of view is unknown. Head tracking runs "
                      "unscaled, which means it will feel more sensitive while zoomed.",
                      kCameraArchetypeName, kCameraArchetypeOuter);
        }
        return;
    }

    const std::uintptr_t archetype = ue_objects::FindLiveObject(
        nullptr, kCameraArchetypeName, kCameraArchetypeOuter);
    if (!archetype) return;

    const std::uintptr_t cls = ue_objects::ClassOf(archetype);
    if (!cls) return;
    ue_reflect::FieldInfo fov;
    if (!ue_reflect::FindPropertyInChain(cls, "FieldOfView", fov)) {
        g_baseFovFailed = true;
        Log::Line("fov: %s is not a camera component - it has no FieldOfView property",
                  kCameraArchetypeName);
        return;
    }
    float value = 0.0f;
    if (!ue::SafeReadFloat(archetype + fov.Offset, value)) return;
    if (!Plausible(value)) {
        g_baseFovFailed = true;
        Log::Line("fov: the camera archetype's FieldOfView reads %.3f, which is not an "
                  "angle - head tracking will run unscaled", value);
        return;
    }
    g_baseFov.store(value, std::memory_order_relaxed);
    Log::Line("fov: the game's un-zoomed field of view is %.3f degrees, read off the "
              "player camera archetype. A zoom scales the head pose by the ratio of "
              "half-field tangents against it, so the picture moves the same distance "
              "zoomed or not.", value);
}

float BaseFov() { return g_baseFov.load(std::memory_order_relaxed); }

float ZoomFactor(float renderedFovDegrees) {
    const float base = g_baseFov.load(std::memory_order_relaxed);
    if (!Plausible(base) || !Plausible(renderedFovDegrees)) {
        g_lastZoom.store(1.0f, std::memory_order_relaxed);
        return 1.0f;
    }
    // Both tangents are of the SAME axis - they are two readings of one engine
    // accessor - so there is no aspect term to get wrong here. The check that
    // says so is the log line below reading 1.0000 in ordinary play.
    const float tanRendered = std::tan(renderedFovDegrees * kHalfDegreeInRadians);
    const float tanBase = std::tan(base * kHalfDegreeInRadians);
    const float factor = cameraunlock::camera::FovZoomFactor(tanRendered, tanBase);
    if (!(factor > 0.0f) || !std::isfinite(factor)) {
        g_lastZoom.store(1.0f, std::memory_order_relaxed);
        return 1.0f;
    }
    g_lastZoom.store(factor, std::memory_order_relaxed);

    // Two lines, because they answer two different questions and gating one on
    // the other throws away the answer that matters.
    //
    // The first is unconditional, on the first frame the camera updates rather
    // than the first frame a pose arrives: a factor wrong by a CONSTANT reads
    // exactly like a factor that is right, so the terms have to be on a line a
    // human can check without a tracker connected. Gating this one on the factor
    // already being 1.0 would print only when nothing is wrong, and a build
    // whose base and live field of view are read on different axes would produce
    // no line at all - which is the fault it exists to expose.
    static std::atomic<bool> s_announced{false};
    if (!s_announced.exchange(true, std::memory_order_relaxed))
        Log::Line("fov: zoom basis - rendered %.4f deg, base %.4f deg, "
                  "tan half %.6f / %.6f, factor %.4f",
                  renderedFovDegrees, base, tanRendered, tanBase, factor);

    // The second is the release gate: the factor must read 1.0000 in ordinary
    // gameplay. It has to be sampled on an UNZOOMED frame to mean that, or a
    // session that starts with the zoom held reports a units fault that is not
    // there.
    static std::atomic<bool> s_unzoomedAnnounced{false};
    if (factor > 0.999f && factor < 1.001f &&
        !s_unzoomedAnnounced.exchange(true, std::memory_order_relaxed))
        Log::Line("fov: unzoomed basis confirmed - rendered %.4f deg, base %.4f deg, "
                  "factor %.4f (this line must read 1.0000)",
                  renderedFovDegrees, base, factor);
    return factor;
}

int AspectConstraint() { return g_constraint.load(std::memory_order_relaxed); }

float GameFov() { return g_gameFov.load(std::memory_order_relaxed); }

float LastZoomFactor() { return g_lastZoom.load(std::memory_order_relaxed); }

}  // namespace subliminal_ht::camera_fov
