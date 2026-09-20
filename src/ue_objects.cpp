// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "ue_objects.h"

#include <unordered_map>

#include "logging.h"

namespace subliminal_ht::ue_objects {

namespace {
namespace ue = ::cameraunlock::unreal;
}

bool ValidateGlobals() {
    const auto& g = Offsets().UObjectGlobals;
    if (ue::ModuleBase() == 0 || g.kObjObjects == 0 || g.kFNamePool == 0 ||
        g.kChunkNumElems == 0 || g.kFUObjectItemSize == 0) {
        Log::Line("objects: this build profile carries no UObject globals - "
                  "the reticle and the lean clamp stay off for this session");
        return false;
    }

    std::uintptr_t chunks = 0;
    std::uint32_t num = 0;
    if (!ReadObjectArrayHeader(chunks, num) || num == 0 || num > kMaxPlausibleObjects) {
        Log::Line("objects: GUObjectArray at RVA 0x%08llx does not read as an object "
                  "table (chunks=0x%llx count=%u) - the reticle and the lean clamp "
                  "stay off for this session",
                  static_cast<unsigned long long>(g.kObjObjects),
                  static_cast<unsigned long long>(chunks), num);
        return false;
    }

    // The name pool is checked by using it: object 0 in a UE build is always a
    // package, and its name resolving to a printable string is what says both
    // the item offset and the pool are right. A silent walk over an empty table
    // is the failure this catches.
    std::uintptr_t chunk0 = 0;
    std::uintptr_t first = 0;
    if (!ReadChunk(chunks, 0, chunk0) ||
        !ue::SafeReadPtr(ItemObjectSlot(chunk0, 0), first) || !first) {
        Log::Line("objects: the first FUObjectItem does not hold an object at +0x%zx",
                  Offsets().kFUObjectItemObject);
        return false;
    }
    const std::string name = ue::ObjectName(first);
    Log::Line("objects: %u live, chunks of %zu, item %zu bytes with the object at "
              "+0x%zx; object 0 is %s %s",
              num, g.kChunkNumElems, g.kFUObjectItemSize, Offsets().kFUObjectItemObject,
              ue::ClassName(first).c_str(), name.c_str());
    return !name.empty();
}

bool RegisteredInObjectArray(std::uintptr_t obj) {
    const auto& g = Offsets().UObjectGlobals;
    if (g.kChunkNumElems == 0 || g.kFUObjectItemSize == 0) return false;

    // UObjectBase packs InternalIndex immediately before ClassPrivate.
    std::uint32_t index = 0;
    if (!ue::SafeReadU32(obj + g.kClassPrivate - 4, index)) return false;

    std::uintptr_t chunks = 0;
    std::uint32_t num = 0;
    if (!ReadObjectArrayHeader(chunks, num) || index >= num) return false;

    std::uintptr_t chunk = 0;
    if (!ReadChunk(chunks, index / static_cast<std::uint32_t>(g.kChunkNumElems), chunk))
        return false;
    std::uintptr_t registered = 0;
    return ue::SafeReadPtr(ItemObjectSlot(chunk, index), registered) && registered == obj;
}

std::uintptr_t FindLiveObject(const char* wantClass, const char* wantName,
                              const char* wantOuter) {
    std::uintptr_t found = 0;
    // The name filter runs against every one of ~180k live objects, and
    // ObjectName resolves the FName through the pool into a fresh std::string
    // every time. Names repeat heavily across the table, so each comparison id
    // is judged once and the verdict reused - the test itself is unchanged, and
    // the class and outer filters keep resolving because they only ever run on
    // the handful of objects the name already matched.
    std::unordered_map<std::uint32_t, bool> judged;
    const std::size_t nameOffset = ue::Layout().kNamePrivate;
    ForEachUObject([&](std::uintptr_t obj) -> bool {
        if (wantName) {
            std::uint32_t id = 0;
            if (!ue::SafeReadU32(obj + nameOffset, id)) return false;
            auto it = judged.find(id);
            if (it == judged.end())
                it = judged.emplace(id, ue::EqualsCI(ue::ResolveFName(id), wantName)).first;
            if (!it->second) return false;
        }
        if (wantClass && !ue::EqualsCI(ue::ClassName(obj), wantClass)) return false;
        if (wantOuter && !ue::EqualsCI(ue::OuterName(obj), wantOuter)) return false;
        found = obj;
        return true;
    });
    return found;
}

}  // namespace subliminal_ht::ue_objects
