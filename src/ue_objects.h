// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

#include <cstdint>
#include <string>

#include <cameraunlock/unreal/ue_runtime.h>

#include "builds/build_registry.h"

// Walking GUObjectArray on a UE 5.7 build.
//
// This exists for one reason: UE 5.7 moved the object pointer inside
// FUObjectItem. The item is still 24 bytes, but UObjectBase* sits eight bytes
// in, behind the flags and serial fields, and core's ForEachUObject reads it at
// item+0 - so core's walk returns the flags word where the object should be and
// finds nothing at all. The offset is in the build profile
// (kFUObjectItemObject) rather than baked in here, because it is the kind of
// engine-version fact a later build can move again.
//
// Everything else about the object graph still comes from core: the
// fault-guarded reads, ResolveFName, ObjectName / ClassName / OuterObject. None
// of those touch the item layout. If core's UObjectGlobalsLayout ever grows an
// item-object offset, this header goes away and its callers switch back.
//
// Game thread only, like every other reflection call in this mod.
namespace subliminal_ht::ue_objects {

// The most live objects a UE build's table can plausibly hold. A count past
// this says the globals in the profile are not GUObjectArray's, and the walk
// stops rather than iterating over whatever the number came from.
inline constexpr std::uint32_t kMaxPlausibleObjects = 0x4000000;

// GUObjectArray's chunk array is an array of pointers.
inline constexpr std::uintptr_t kChunkPointerSize = 8;

// The chunk array and the live object count at the head of GUObjectArray.
// `chunks` and `num` are zeroed first, so a caller can report what it did read
// when this returns false. The count is NOT range-checked here - the walking
// callers do that themselves, and an index lookup is already bounded by it.
inline bool ReadObjectArrayHeader(std::uintptr_t& chunks, std::uint32_t& num) {
    namespace ue = ::cameraunlock::unreal;
    chunks = 0;
    num = 0;
    if (ue::ModuleBase() == 0) return false;
    const auto& g = Offsets().UObjectGlobals;
    const std::uintptr_t objArr = ue::ModuleBase() + g.kObjObjects;
    if (!ue::SafeReadPtr(objArr, chunks) || !chunks) return false;
    return ue::SafeReadU32(objArr + g.kObjObjects_Num, num);
}

// One chunk of FUObjectItems out of the chunk array.
inline bool ReadChunk(std::uintptr_t chunks, std::uint32_t chunkIndex,
                      std::uintptr_t& chunk) {
    chunk = 0;
    return ::cameraunlock::unreal::SafeReadPtr(
               chunks + static_cast<std::uintptr_t>(chunkIndex) * kChunkPointerSize, chunk)
        && chunk != 0;
}

// Address of the UObjectBase* slot inside the FUObjectItem holding object
// `index`, given the chunk that index falls in.
inline std::uintptr_t ItemObjectSlot(std::uintptr_t chunk, std::uint32_t index) {
    const auto& g = Offsets().UObjectGlobals;
    return chunk
         + static_cast<std::uintptr_t>(index % g.kChunkNumElems) * g.kFUObjectItemSize
         + Offsets().kFUObjectItemObject;
}

// An object's UClass, or 0 when it does not read.
inline std::uintptr_t ClassOf(std::uintptr_t obj) {
    namespace ue = ::cameraunlock::unreal;
    std::uintptr_t cls = 0;
    if (!ue::SafeReadPtr(obj + ue::Layout().kClassPrivate, cls)) return 0;
    return cls;
}

// True when the profile's object-table globals read back as an object table:
// a chunk array, a plausible element count, and non-zero divisors. Callers that
// walk on a timer check this once and disable themselves rather than repeating
// a walk that cannot work.
bool ValidateGlobals();

// Visit every live UObject. visit(obj) returns true to stop early.
template <typename Fn>
void ForEachUObject(Fn&& visit) {
    namespace ue = ::cameraunlock::unreal;
    const auto& g = Offsets().UObjectGlobals;
    // Both are divisors below, and the item offset is added to a raw address:
    // a profile that misses one of these would divide by zero inside the loop,
    // outside every SafeRead guard.
    if (g.kChunkNumElems == 0 || g.kFUObjectItemSize == 0) return;

    std::uintptr_t chunks = 0;
    std::uint32_t num = 0;
    if (!ReadObjectArrayHeader(chunks, num)) return;
    if (num == 0 || num > kMaxPlausibleObjects) return;

    // The chunk pointer is held across the run of indices that share it. This
    // walk visits every live object in the process, so re-reading it per index
    // would cost a guarded load per object for nothing.
    std::uintptr_t chunk = 0;
    std::uint32_t chunkIndex = 0xffffffffu;
    for (std::uint32_t i = 0; i < num; ++i) {
        const std::uint32_t wantChunk = i / static_cast<std::uint32_t>(g.kChunkNumElems);
        if (wantChunk != chunkIndex) {
            chunkIndex = wantChunk;
            if (!ReadChunk(chunks, wantChunk, chunk)) chunk = 0;
        }
        if (!chunk) continue;
        std::uintptr_t obj = 0;
        if (!ue::SafeReadPtr(ItemObjectSlot(chunk, i), obj) || !obj) continue;
        if (visit(obj)) return;
    }
}

// The array item an object's own InternalIndex points at, or 0 when the object
// is not registered. GUObjectArray is the authority on whether an object still
// exists: freed UObject memory keeps its old class pointer for as long as the
// allocator leaves it alone, so a class-pointer test on its own reports a
// destroyed widget as live indefinitely.
bool RegisteredInObjectArray(std::uintptr_t obj);

// First object whose name matches, and whose class and outer chain match when
// given. wantClass and wantOuter may be null.
std::uintptr_t FindLiveObject(const char* wantClass, const char* wantName,
                              const char* wantOuter);

}  // namespace subliminal_ht::ue_objects
