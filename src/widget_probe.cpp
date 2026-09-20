// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

// Find the UMG widgets that need unsticking.
//
// The reticle and the interaction prompt are Blueprint assets in the cooked
// pak, not native classes - the exec thunks behind SetReticleSize are generic
// Blueprint VM stubs, and the widget variable names exist only in the .uasset.
// So the names cannot be recovered from the EXE and have to be read off the
// live object table with the game running.

#include "widget_probe.h"

#include <cstdint>
#include <string>
#include <unordered_set>

#include "logging.h"
#include "ue_objects.h"
#include "ue_vm.h"

#include "cameraunlock/unreal/ue_runtime.h"

namespace subliminal_ht::widget_probe {

namespace {

namespace ue = ::cameraunlock::unreal;

// Substrings worth looking at. Deliberately wide: this runs a handful of times
// in a dev session, and a name we did not anticipate is exactly what it exists
// to catch.
const char* kNeedles[] = {
    "reticle", "crosshair", "cursor", "prompt", "tooltip",
    "hint", "interact", "hud", "marker",
};

// How far up the outer chain a candidate is reported. Three links is enough to
// tell a live widget from the Blueprint template of the same name.
constexpr int kOuterChainDepth = 3;

}  // namespace

void DumpCandidates() {
    static std::unordered_set<std::uintptr_t> s_seen;
    int hits = 0, fresh = 0;
    ue_objects::ForEachUObject([&](std::uintptr_t obj) -> bool {
        const std::string on = ue::ObjectName(obj);
        if (on.empty()) return false;
        const std::string cn = ue::ClassName(obj);

        bool match = false;
        for (const char* n : kNeedles) {
            if (ue::ContainsCI(on, n) || ue::ContainsCI(cn, n)) { match = true; break; }
        }
        if (!match) return false;
        ++hits;
        // Archetypes and class-default objects are not on screen; they are
        // noise here, but their names are the same as the instances', so they
        // are counted and not printed rather than dropped silently.
        if (ue::ContainsCI(on, "Default__")) return false;
        if (!s_seen.insert(obj).second) return false;
        ++fresh;
        Log::Line("  widget 0x%llx  class=%-40s name=%s%s",
            static_cast<unsigned long long>(obj), cn.c_str(), on.c_str(),
            ue_vm::OuterChain(obj, kOuterChainDepth, " < ").c_str());
        return false;
    });
    Log::Line("widget-probe: %d matching objects, %d newly listed", hits, fresh);
}

}  // namespace subliminal_ht::widget_probe
