// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

#include <cstddef>
#include <cstdint>

#include "logging.h"
#include "ue_objects.h"
#include "ue_reflect.h"

// What the two Kismet traces in this mod have in common.
//
// aim_trace runs UKismetSystemLibrary::LineTraceSingle to find where the shot
// lands; lean_trace runs SphereTraceSingle to find how far the eye may lean.
// Both dispatch a UFUNCTION on the same class-default object, both build a
// parameter frame in a stack buffer at offsets read out of the engine's own
// reflection data, and both have to prove that frame is the shape they are
// about to write before writing it. That shared scaffolding lives here; the
// parameters, the fields read back out of FHitResult and what each does with
// the answer stay in their own files, because they are what the two traces
// actually differ in.
namespace subliminal_ht::kismet_trace {

// The parameter frame of a Kismet trace is a couple of hundred bytes. This is
// the ceiling the resolvers check PropertiesSize against, so a garbage size
// cannot turn into a stack overflow.
inline constexpr std::size_t kMaxParams = 1024;

// TArray's header, as the engine lays it out. Only ever handed to a parameter
// the trace reads and never reallocates, so there is nothing here for the
// engine to free.
struct alignas(8) TArrayHeader {
    void*        Data;
    std::int32_t Num;
    std::int32_t Max;
};

// One claim that a resolved parameter can hold the fixed-size C++ type the
// caller is about to put in it. `Index` is the parameter's position in the list
// handed to ue_reflect::ResolveAll.
struct ExpectedWidth {
    const char* Name;
    std::size_t Index;
    std::size_t Bytes;
};

// The class-default object both traces dispatch against.
inline std::uintptr_t FindKismetSystemLibraryCdo() {
    return ue_objects::FindLiveObject("KismetSystemLibrary",
                                      "Default__KismetSystemLibrary", nullptr);
}

// ResolveAll proves each named parameter sits inside the frame at the width the
// ENGINE reports. This is the other claim: that each one can hold the fixed-size
// type the caller writes into it. A slot narrower than the type runs the tail of
// that write off the end of the stack buffer, so it is checked before any of the
// frame is written. Returns false, having named the parameter that failed, so
// the caller can stand its feature down.
template <std::size_t N, typename FieldInfoRange>
bool FrameFitsWrites(const char* label, const FieldInfoRange& resolved,
                     const ExpectedWidth (&widths)[N], std::size_t paramsSize) {
    for (const ExpectedWidth& w : widths) {
        if (ue_reflect::FieldFits(resolved[w.Index], w.Bytes, paramsSize)) continue;
        Log::Line("%s: %s is %zu bytes at +0x%zx in a %zu-byte frame, which cannot "
                  "hold the %zu bytes this mod writes there",
                  label, w.Name, resolved[w.Index].Size, resolved[w.Index].Offset,
                  paramsSize, w.Bytes);
        return false;
    }
    return true;
}

}  // namespace subliminal_ht::kismet_trace
