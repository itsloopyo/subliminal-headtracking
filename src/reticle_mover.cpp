// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

// Unstick the crosshair.
//
// The interaction ray keeps the clean mouse-driven rotation while the view
// follows the head, so the ring drawn at the centre of the picture stops
// marking the ray the moment the player looks off-centre. UMG anchors it there,
// so the fix is to translate the widget by the offset the aim projection
// computes.
//
// Subliminal's ring is not a widget of its own. HUD_C carries a full-screen
// Image called GUI whose brush is the material M_HUD, and the ring's radius,
// thickness, opacity and colour come from the PC_HUD material parameter
// collection - which has no position parameter, so the ring cannot be offset
// through the material. Translating the Image moves it, and moves nothing else:
// the vignette, scanlines and grain are post-process, and the icons, bars and
// key prompts are separate widgets in the same tree.
//
// Those prompts are deliberately left alone. They are screen furniture anchored
// to the edges of the frame - "E  OPEN" at the bottom, the flashlight and
// stamina icons in the corners - not marks on the thing being looked at, and
// sliding them with the head would be wrong rather than helpful.
//
// The trap is that GUI exists twice while the game is running: once as the
// template inside the loaded widget Blueprint and once as the widget the HUD
// actually built. Walking the live object table finds both. Measured outer
// chains:
//
//   template  WidgetTree / HUD_C / HUD / /Game/Blueprints/UI/HUD/HUD
//   live      WidgetTree / HUD_C / GameInstance_Main_C / GameEngine
//
// so the name matches both and GameInstance_Main is what tells them apart. Pick
// the template and the ring sits dead at screen centre with nothing in the log
// to say why.

#include "reticle_mover.h"

#include <cstdint>
#include <string>

#include <windows.h>

#include "aim_projection.h"
#include "builds/build_registry.h"
#include "logging.h"
#include "ue_objects.h"
#include "ue_reflect.h"
#include "ue_vm.h"

#include "cameraunlock/unreal/ue_runtime.h"

namespace subliminal_ht::reticle_mover {

namespace {

namespace ue = ::cameraunlock::unreal;

// The widget that follows the aim, by object name plus the outer that tells
// the live one from the Blueprint template (see the header comment).
struct Target { const char* Name; const char* Outer; };
constexpr Target kTargets[] = {
    { "GUI", "GameInstance_Main" },
};
constexpr std::size_t kNumTargets = sizeof(kTargets) / sizeof(kTargets[0]);

// How often the object table is re-walked to re-find the targets: the short
// interval once a held widget has failed its liveness test, the long one as a
// backstop while everything still looks fine. The backstop is what bounds how
// long the reticle can sit dead at screen centre if a rebuilt HUD ever leaves
// the widget we hold alive but no longer painted, which no test on the pointer
// itself can see. A walk costs about 5ms, so 15s of it is not worth measuring;
// running it every couple of seconds regardless would be.
constexpr std::uint64_t kRetryWalkMs    = 2000;
constexpr std::uint64_t kBackstopWalkMs = 15000;

// How often a push happens anyway with the offset unchanged, so a game-side
// reset of RenderTransform cannot stick. Counted in calls, which arrive at
// frame rate.
constexpr std::uint64_t kReassertEveryCalls = 120;

// The offset line is evidence that the widgets follow the aim, and that reads
// off the first few lines; the heartbeat carries liveness for the rest of the
// session. Bounded on top of its interval because counting pushes tied the
// rate to the frame rate: 200 of the 275 lines in a seven-minute session were
// this one line, repeating that the reticle was still following.
constexpr std::uint64_t kOffsetLogMs    = 2000;
constexpr int           kOffsetLogLines = 20;

// A widget plus the class pointer it carried when collected. Loading a level
// frees and recreates the HUD, so a held pointer can dangle or be reused for a
// different object.
struct Widget { std::uintptr_t Obj = 0; std::uintptr_t Cls = 0; };

Widget g_widgets[kNumTargets];

// FName comparison ids for the target names, learned by the first walk that
// finds them. ObjectName() ignores the FName number, so comparing the id is
// the same test as the string compare that learned it, minus a name-pool
// lookup and a std::string build for every one of ~100k objects - which is
// what makes re-walking on a timer affordable.
std::uint32_t g_nameIds[kNumTargets] = {};

// How long the last walk took, reported in the throttled offset line so the
// cost of running it repeatedly stays visible rather than assumed.
float g_lastWalkMs = 0.0f;
std::uintptr_t g_setRenderTranslationFn = 0;
std::uintptr_t g_getViewportScaleFn = 0;
std::uintptr_t g_widgetLayoutLibCdo = 0;

// UMG paints a widget's render translation in slate units, which become
// units * DPI-scale real pixels, while the projected offset is real pixels. The
// movers divide by this. UE's default curve gives 1.0 at 1080p and 1.333 at
// 1440p-tall, so ignoring it overshoots by a third on a 1440p display.
float g_dpiScale = 1.0f;

// The two parameter frames this file hands ProcessEvent, as fixed-size stack
// structs. ProcessEvent copies a UFUNCTION's whole parameter block through the
// pointer it is given, so a function whose frame is wider than the struct below
// would have the tail of that copy land past the end of it - on the game
// thread, every frame the reticle moves. The engine reports its own width, so
// it is asked once when each function is found, exactly as the two Kismet
// traces ask before writing their frames. Neither is checked by anything else:
// these two are dispatched at layouts this mod assumes rather than at offsets
// read out of the reflection data.
//
// The trailing padding covers a parameter the real signature carries that we do
// not set - it is headroom, not a licence to skip the check.
struct SetRenderTranslationParams { double X; double Y; char pad[16]; };
struct GetViewportScaleParams { void* WorldContext; float Ret; char pad[12]; };

bool g_translationFrameFits = false;
bool g_viewportScaleFrameFits = false;

bool FrameFits(const char* label, std::uintptr_t fn, std::size_t capacity) {
    const std::size_t size = ue_reflect::StructSize(fn);
    if (size != 0 && size <= capacity) return true;
    Log::Line("reticle: %s has a %zu-byte parameter frame and this mod hands "
              "ProcessEvent %zu bytes - not dispatching it, the reticle stays where "
              "the game draws it", label, size, capacity);
    return false;
}

// What a viewport scale can believably be. UE's default curve gives 1.0 at
// 1080p and 1.333 at 1440p-tall; anything outside this says the call did not
// return what we think it did.
constexpr float kMinPlausibleDpiScale = 0.05f;
constexpr float kMaxPlausibleDpiScale = 20.0f;

bool Live(const Widget& w) {
    if (!w.Obj || !w.Cls) return false;
    if (ue_objects::ClassOf(w.Obj) != w.Cls) return false;
    // GUObjectArray is the authority on whether an object still exists. Freed
    // UObject memory keeps its old class pointer for as long as the allocator
    // leaves it alone, so a class-pointer test on its own reports a destroyed
    // widget as live indefinitely - the mod would then push translations into a
    // widget nothing paints, and the log would say everything is fine.
    return ue_objects::RegisteredInObjectArray(w.Obj);
}

// How far up the outer chain the target test looks. Four links reach the map
// name, which is where the live widget and its Blueprint template part company.
constexpr int kOuterChainDepth = 4;

// The UFUNCTIONs the movers dispatch, plus the class-default object the
// viewport-scale call needs a `this` for. Each is looked up once and kept: they
// live for the process, unlike the widgets, which a level change replaces.
void ResolveScriptFunctions() {
    if (!g_setRenderTranslationFn) {
        g_setRenderTranslationFn = ue_objects::FindLiveObject("Function", "SetRenderTranslation", "Widget");
        if (g_setRenderTranslationFn)
            g_translationFrameFits = FrameFits("SetRenderTranslation", g_setRenderTranslationFn,
                                               sizeof(SetRenderTranslationParams));
    }
    if (!g_getViewportScaleFn) {
        g_getViewportScaleFn = ue_objects::FindLiveObject("Function", "GetViewportScale", "WidgetLayoutLibrary");
        if (g_getViewportScaleFn)
            g_viewportScaleFrameFits = FrameFits("GetViewportScale", g_getViewportScaleFn,
                                                 sizeof(GetViewportScaleParams));
    }
    if (!g_widgetLayoutLibCdo)
        g_widgetLayoutLibCdo = ue_objects::FindLiveObject("WidgetLayoutLibrary", "Default__WidgetLayoutLibrary", nullptr);
}

// One table walk collects every target; doing it per widget would walk 100k
// objects once each. Only objects the array still holds are visited, so a
// widget that has been destroyed cannot come back out of this.
void Collect() {
    LARGE_INTEGER freq{}, t0{}, t1{};
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&t0);

    Widget found[kNumTargets];
    int matches[kNumTargets] = {};
    const std::size_t nameOff = Offsets().UObjectGlobals.kNamePrivate;
    ue_objects::ForEachUObject([&](std::uintptr_t obj) -> bool {
        std::uint32_t id = 0;
        if (!ue::SafeReadU32(obj + nameOff, id)) return false;
        std::string name;
        for (std::size_t i = 0; i < kNumTargets; ++i) {
            if (g_nameIds[i] != 0) {
                if (id != g_nameIds[i]) continue;
            } else {
                if (name.empty()) name = ue::ResolveFName(id);
                if (name != kTargets[i].Name) continue;
            }
            if (!ue::ContainsCI(ue_vm::OuterChain(obj, kOuterChainDepth, "/"),
                                kTargets[i].Outer)) continue;
            const std::uintptr_t cls = ue_objects::ClassOf(obj);
            if (cls) {
                found[i] = {obj, cls};
                g_nameIds[i] = id;
                ++matches[i];
            }
            break;
        }
        return false;
    });

    QueryPerformanceCounter(&t1);
    g_lastWalkMs = freq.QuadPart
        ? static_cast<float>((t1.QuadPart - t0.QuadPart) * 1000.0 / static_cast<double>(freq.QuadPart))
        : 0.0f;

    bool changed = false;
    for (std::size_t i = 0; i < kNumTargets; ++i)
        if (found[i].Obj != g_widgets[i].Obj) changed = true;

    ResolveScriptFunctions();

    if (changed) {
        // Both addresses, so a session log shows the moment a rebuilt HUD moved
        // a widget and what it moved to. matches says whether more than one
        // object answered to the name and outer, which would make the choice
        // below arbitrary.
        for (std::size_t i = 0; i < kNumTargets; ++i) {
            Log::Line("reticle: target %-20s 0x%llx -> 0x%llx  matches=%d%s", kTargets[i].Name,
                static_cast<unsigned long long>(g_widgets[i].Obj),
                static_cast<unsigned long long>(found[i].Obj), matches[i],
                found[i].Obj ? "" : "  (NOT FOUND - stays screen-fixed)");
        }
        Log::Line("reticle: setRenderTranslation=0x%llx dpiScale=%.3f walk=%.1fms",
            static_cast<unsigned long long>(g_setRenderTranslationFn), g_dpiScale, g_lastWalkMs);
    }

    for (std::size_t i = 0; i < kNumTargets; ++i) g_widgets[i] = found[i];
}

void RefreshDpiScale() {
    if (!g_getViewportScaleFn || !g_viewportScaleFrameFits || !g_widgetLayoutLibCdo) return;
    // GetViewportScale finds the viewport through its world-context object, so
    // there is nothing to ask while the walk has not found a widget to pass.
    if (!g_widgets[0].Obj) return;
    GetViewportScaleParams p{};
    p.WorldContext = reinterpret_cast<void*>(g_widgets[0].Obj);
    if (!ue_vm::Dispatch(reinterpret_cast<void*>(g_widgetLayoutLibCdo),
                         reinterpret_cast<void*>(g_getViewportScaleFn), &p))
        return;
    // A zero or absurd scale means the call did not do what we think; keeping
    // the previous value is better than dividing the offset by nonsense.
    if (p.Ret > kMinPlausibleDpiScale && p.Ret < kMaxPlausibleDpiScale
        && p.Ret != g_dpiScale) {
        Log::Line("reticle: viewport DPI scale %.3f -> %.3f", g_dpiScale, p.Ret);
        g_dpiScale = p.Ret;
    }
}

void Push(Widget& w, double x, double y) {
    if (!Live(w)) return;
    // UE5 LWC: FVector2D is two doubles.
    SetRenderTranslationParams tr{};
    tr.X = x;
    tr.Y = y;
    if (ue_vm::Dispatch(reinterpret_cast<void*>(w.Obj),
                        reinterpret_cast<void*>(g_setRenderTranslationFn), &tr))
        return;

    // A dispatch that faults means the object stopped being what it was between
    // the liveness test and the call. Dropping it sends the next walk looking
    // for whatever replaced it; swallowing the fault would leave the reticle
    // pinned to a dead object with nothing in the log to say so.
    Log::Line("reticle: SetRenderTranslation faulted on 0x%llx - dropped, re-locating",
        static_cast<unsigned long long>(w.Obj));
    w = Widget{};
}

// Resolve the script VM on the first tick that can, and say where it landed.
bool VmReady() {
    if (!ue_vm::Ready()) return false;
    static bool s_announced = false;
    if (!s_announced) {
        s_announced = true;
        Log::Line("reticle: ProcessEvent at RVA 0x%08llx",
            static_cast<unsigned long long>(ue_vm::ProcessEventRva()));
    }
    return true;
}

// A level change rebuilds the HUD, and the widget that replaces the one we hold
// is a different object. That is what the liveness test is for, and the short
// retry interval then re-points within a couple of seconds - it also covers a
// target that is genuinely absent, since the prompt manager does not exist until
// the HUD is up. The walk runs on the backstop interval even when every target
// looks fine, because a widget that has been orphaned rather than destroyed
// reads as perfectly alive. Comparing FName ids rather than resolving ~180k
// names is what makes repeating the walk affordable; its cost is printed in the
// offset line, so it can be checked rather than trusted.
void MaybeRefreshTargets() {
    bool allLive = true;
    for (const Widget& w : g_widgets) if (!Live(w)) { allLive = false; break; }

    static std::uint64_t s_lastWalk = 0;
    const std::uint64_t now = GetTickCount64();
    if (now - s_lastWalk < (allLive ? kBackstopWalkMs : kRetryWalkMs)) return;
    s_lastWalk = now;
    Collect();
    RefreshDpiScale();
}

// The hook fires several times per frame but the offset only changes on the
// render caller, and every push is a script-VM dispatch. Skip the pass when
// nothing moved, but re-assert every kReassertEveryCalls so a game-side reset of
// RenderTransform cannot stick while the offset is static.
bool NeedsPush(double x, double y) {
    static double s_lastX = 0.0, s_lastY = 0.0;
    static bool s_written = false;
    static std::uint64_t s_calls = 0;
    const bool reassert = (s_calls++ % kReassertEveryCalls) == 0;
    if (s_written && !reassert && x == s_lastX && y == s_lastY) return false;
    s_lastX = x; s_lastY = y; s_written = true;
    return true;
}

}  // namespace

void Tick() {
    if (Offsets().UObjectGlobals.kObjObjects == 0) return;
    if (!VmReady()) return;

    MaybeRefreshTargets();
    if (!g_setRenderTranslationFn || !g_translationFrameFits || !Live(g_widgets[0])) return;

    float dx = 0.0f, dy = 0.0f;
    const bool haveOffset = aim_projection::GetScreenOffset(dx, dy);
    // No valid offset means tracking is off, suppressed, or the aim is behind
    // the view. Park the widgets back at the centre rather than leaving them
    // stuck wherever the last tracked frame put them.
    const double slateX = haveOffset ? dx / g_dpiScale : 0.0;
    const double slateY = haveOffset ? dy / g_dpiScale : 0.0;
    if (!NeedsPush(slateX, slateY)) return;

    for (Widget& w : g_widgets) Push(w, slateX, slateY);

    static std::uint64_t s_lastOffsetLog = 0;
    static int s_offsetLines = 0;
    if (s_offsetLines >= kOffsetLogLines) return;
    const std::uint64_t now = GetTickCount64();
    if (s_offsetLines != 0 && now - s_lastOffsetLog < kOffsetLogMs) return;
    s_lastOffsetLog = now;
    ++s_offsetLines;
    Log::Line("reticle: offset px=(%.1f,%.1f) slate=(%.1f,%.1f) dpi=%.3f valid=%s walk=%.1fms",
        dx, dy, slateX, slateY, g_dpiScale, haveOffset ? "yes" : "no", g_lastWalkMs);
}

}  // namespace subliminal_ht::reticle_mover
