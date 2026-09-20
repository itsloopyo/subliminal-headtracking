// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "aim_projection.h"

#include <algorithm>
#include <atomic>
#include <mutex>

#include <windows.h>

#include "camera_fov.h"
#include "logging.h"

#include "cameraunlock/os/game_window.h"

namespace subliminal_ht::aim_projection {

namespace {

std::mutex g_mutex;
Frame g_frame;
bool  g_active = false;
// 0 until a render-caller frame has read one. Not a seeded default: a
// projection against a made-up field of view moves the crosshair somewhere the
// player is not pointing, and leaving it where the game drew it beats that.
float g_fovDegrees = 0.0f;

std::atomic<float> g_offsetX{0.0f};
std::atomic<float> g_offsetY{0.0f};
std::atomic<bool>  g_valid{false};

// Said once per reason, because "the crosshair is not moving" has two honest
// answers and they need different fixes.
void ReportNoTangents(float fov) {
    static std::atomic<bool> s_saidNoFov{false};
    static std::atomic<bool> s_saidConstraint{false};
    if (!camera_fov::Plausible(fov)) {
        if (!s_saidNoFov.exchange(true, std::memory_order_relaxed))
            Log::Line("aim-projection: no usable field of view yet (%.1f degrees), so "
                      "nothing is being projected", fov);
        return;
    }
    if (!s_saidConstraint.exchange(true, std::memory_order_relaxed))
        Log::Line("aim-projection: the engine's aspect constraint has not been read, "
                  "so which axis the field of view describes is unknown and nothing "
                  "will be projected until it resolves.");
}

// The game's window, held between frames.
//
// Core's finder skips the splash by enumerating every top-level window on the
// desktop and testing four properties of each, and this runs on the game thread
// once per rendered frame - a few hundred user32 calls a frame, for a handle
// that changes at most a handful of times a session. So the handle is kept and
// only re-found once it stops being a live visible window, which is what UE
// leaves behind on a fullscreen-mode change.
//
// Caller holds g_mutex, which is what makes the static safe.
HWND GameWindowLocked() {
    static HWND s_window = nullptr;
    if (s_window && IsWindow(s_window) && IsWindowVisible(s_window)) return s_window;
    s_window = cameraunlock::os::FindGameWindow();
    return s_window;
}

// Caller holds g_mutex.
void RecomputeLocked() {
    if (!g_active) {
        g_valid.store(false, std::memory_order_relaxed);
        return;
    }
    RECT rc{};
    const HWND wnd = GameWindowLocked();
    if (!wnd || !GetClientRect(wnd, &rc) || rc.right <= 0 || rc.bottom <= 0) {
        g_valid.store(false, std::memory_order_relaxed);
        return;
    }
    const float w = static_cast<float>(rc.right);
    const float h = static_cast<float>(rc.bottom);

    float tanX = 0.0f, tanY = 0.0f;
    if (!camera_fov::HalfFieldTangents(camera_fov::AspectConstraint(), g_fovDegrees,
                                       w / h, tanX, tanY)) {
        ReportNoTangents(g_fovDegrees);
        g_valid.store(false, std::memory_order_relaxed);
        return;
    }

    // With a hit this is the whole parallax term and the rotation term at once:
    // the vector from the eye the frame is DRAWN from to the point the aim ray
    // actually stopped on. With no hit the ray reached nothing, so the target is
    // at infinity and its direction is all there is - which is also the frame
    // where a lean genuinely does not move it.
    const ue::FVector v = g_frame.HasPoint
        ? ue::FVector{g_frame.Point.X - g_frame.RenderedEye.X,
                      g_frame.Point.Y - g_frame.RenderedEye.Y,
                      g_frame.Point.Z - g_frame.RenderedEye.Z}
        : g_frame.Direction;

    float ndcX = 0.0f, ndcY = 0.0f;
    if (!ProjectFromRenderedEye(g_frame.TrackedRotation, v, tanX, tanY, ndcX, ndcY)) {
        // The head has turned more than 90 degrees off the aim. There is no
        // screen position for it, so nothing is moved.
        g_valid.store(false, std::memory_order_relaxed);
        return;
    }

    // Clamp at the viewport edge. Without it, an aim approaching 90 degrees off
    // axis sends the quotient into the thousands and the crosshair accelerates
    // away off screen instead of pinning to the edge the player should turn
    // back towards.
    ndcX = std::clamp(ndcX, -1.0f, 1.0f);
    ndcY = std::clamp(ndcY, -1.0f, 1.0f);

    g_offsetX.store(ndcX * w * 0.5f, std::memory_order_relaxed);
    g_offsetY.store(-ndcY * h * 0.5f, std::memory_order_relaxed);
    g_valid.store(true, std::memory_order_relaxed);
}

}  // namespace

void Update(const Frame& frame, float fovDegrees, bool active) {
    std::lock_guard<std::mutex> lk(g_mutex);
    g_frame = frame;
    g_active = active;
    // A frame whose FOV read failed pushes 0, which invalidates the projection
    // rather than leaving it running on the last good value.
    g_fovDegrees = camera_fov::Plausible(fovDegrees) ? fovDegrees : 0.0f;
    RecomputeLocked();
}

void SetActive(bool active) {
    std::lock_guard<std::mutex> lk(g_mutex);
    g_active = active;
    RecomputeLocked();
}

bool GetScreenOffset(float& dx, float& dy) {
    if (!g_valid.load(std::memory_order_relaxed)) return false;
    dx = g_offsetX.load(std::memory_order_relaxed);
    dy = g_offsetY.load(std::memory_order_relaxed);
    return true;
}

}  // namespace subliminal_ht::aim_projection
