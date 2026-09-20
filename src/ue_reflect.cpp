// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "ue_reflect.h"

#include <cstdint>

#include "builds/build_registry.h"
#include "logging.h"

#include "cameraunlock/unreal/ue_runtime.h"

namespace subliminal_ht::ue_reflect {

namespace {

namespace ue = ::cameraunlock::unreal;

// A property chain longer than this is not a property chain. UE's largest
// reflected structs are a few hundred fields; anything past this means the
// Next offset is wrong and the walk is following heap noise, so it stops
// rather than reading until it faults.
constexpr int kMaxChain = 4096;

// How far up the SuperStruct chain FindPropertyInChain climbs. Bounded rather
// than "until SuperStruct is null": a wrong kUStruct_SuperStruct reads a
// pointer-shaped field that happens to point back into the object graph, and
// the walk then never terminates. UE's deepest shipped hierarchies are a dozen
// or so classes.
constexpr int kMaxSuperChain = 32;

// The largest PropertiesSize that is a real UE struct. Past this the number is
// not a struct size, and everything measured against it would be nonsense.
constexpr std::size_t kMaxStructSize = 0x10000;

std::string FieldName(std::uintptr_t field) {
    std::uint32_t id = 0;
    if (!ue::SafeReadU32(field + Offsets().Reflection.kFField_NamePrivate, id)) return {};
    return ue::ResolveFName(id);
}

std::string FieldTypeName(std::uintptr_t field) {
    std::uintptr_t cls = 0;
    if (!ue::SafeReadPtr(field + Offsets().Reflection.kFField_ClassPrivate, cls) || !cls)
        return {};
    std::uint32_t id = 0;
    if (!ue::SafeReadU32(cls + Offsets().Reflection.kFFieldClass_Name, id)) return {};
    return ue::ResolveFName(id);
}

// Everything recorded about one field: two FName resolves, so two strings off
// the heap, plus three dword reads. Filled for a field that has already MATCHED
// rather than for every field walked past - see FindProperty.
FieldInfo ReadField(std::uintptr_t field) {
    const auto& r = Offsets().Reflection;
    FieldInfo info;
    info.Field    = field;
    info.Name     = FieldName(field);
    info.TypeName = FieldTypeName(field);

    std::uint32_t offset = 0, elementSize = 0, arrayDim = 0;
    if (ue::SafeReadU32(field + r.kFProperty_Offset, offset) &&
        ue::SafeReadU32(field + r.kFProperty_ElementSize, elementSize) &&
        ue::SafeReadU32(field + r.kFProperty_ArrayDim, arrayDim) &&
        // A real FProperty always declares at least one element, so a zero here
        // is not a degenerate array - it is evidence that kFProperty_ArrayDim is
        // pointed at something that is not the property. Substituting 1 would
        // turn that evidence into a plausible Size == ElementSize, which then
        // passes FieldFits and ResolveAll's own zero-size rejection, and the
        // Kismet frame gets written at offsets nothing validated.
        arrayDim != 0) {
        info.Offset = offset;
        info.Size   = static_cast<std::size_t>(elementSize) * arrayDim;
    }
    return info;
}

// The FField chain hanging off a UStruct, in declaration order. `visit(field)`
// returns true to stop early.
template <typename Fn>
void ForEachField(std::uintptr_t ustruct, Fn&& visit) {
    if (!ustruct) return;
    const auto& r = Offsets().Reflection;

    std::uintptr_t field = 0;
    if (!ue::SafeReadPtr(ustruct + r.kUStruct_ChildProperties, field)) return;

    for (int i = 0; field && i < kMaxChain; ++i) {
        if (visit(field)) return;
        std::uintptr_t next = 0;
        if (!ue::SafeReadPtr(field + r.kFField_Next, next)) return;
        field = next;
    }
}

// Walks the chain and reads only each field's NAME until one matches, rather
// than materialising the whole property table and searching it. The table is
// two heap strings per field, and the callers ask repeatedly against the same
// struct - ResolveAll once per name it wants, FindPropertyInChain once per
// class above the leaf - so building it per lookup was the whole cost of
// resolving a ten-parameter UFunction.
bool FindProperty(std::uintptr_t ustruct, const char* name, FieldInfo& out) {
    bool found = false;
    ForEachField(ustruct, [&](std::uintptr_t field) {
        if (!ue::EqualsCI(FieldName(field), name)) return false;
        out = ReadField(field);
        found = true;
        return true;
    });
    return found;
}

// Every reflected property of a UStruct, in declaration order. Empty when the
// struct pointer or the reflection layout does not read.
std::vector<FieldInfo> Properties(std::uintptr_t ustruct) {
    std::vector<FieldInfo> out;
    ForEachField(ustruct, [&out](std::uintptr_t field) {
        out.push_back(ReadField(field));
        return false;
    });
    return out;
}

}  // namespace

std::size_t StructSize(std::uintptr_t ustruct) {
    if (!ustruct) return 0;
    std::uint32_t size = 0;
    if (!ue::SafeReadU32(ustruct + Offsets().Reflection.kUStruct_PropertiesSize, size))
        return 0;
    return size;
}

bool FindPropertyInChain(std::uintptr_t ustruct, const char* name, FieldInfo& out) {
    const auto& r = Offsets().Reflection;
    std::uintptr_t cur = ustruct;
    for (int depth = 0; cur && depth < kMaxSuperChain; ++depth) {
        if (FindProperty(cur, name, out)) return true;
        std::uintptr_t super = 0;
        if (!ue::SafeReadPtr(cur + r.kUStruct_SuperStruct, super)) return false;
        cur = super;
    }
    return false;
}

void DumpProperties(const char* label, std::uintptr_t ustruct) {
    const std::size_t size = StructSize(ustruct);
    Log::Line("reflect: %s @0x%llx PropertiesSize=%zu", label,
        static_cast<unsigned long long>(ustruct), size);
    for (const FieldInfo& f : Properties(ustruct)) {
        Log::Line("reflect:   +0x%03zx size %-4zu %-18s %s",
            f.Offset, f.Size, f.TypeName.c_str(), f.Name.c_str());
    }
}

bool ResolveAll(const char* label, std::uintptr_t ustruct,
                const std::vector<std::string>& names,
                std::vector<FieldInfo>& out) {
    out.clear();
    const std::size_t size = StructSize(ustruct);
    // A reflected struct with no size, or one bigger than any real UE struct,
    // says PropertiesSize is not where the profile claims. Everything below
    // would then be measured against nonsense.
    if (size == 0 || size > kMaxStructSize) {
        Log::Line("reflect: %s has PropertiesSize=%zu, which is not a struct size - "
                  "the ReflectionLayout in this build profile does not fit this game "
                  "build", label, size);
        DumpProperties(label, ustruct);
        return false;
    }

    bool ok = true;
    for (const std::string& want : names) {
        FieldInfo f;
        if (!FindProperty(ustruct, want.c_str(), f)) {
            Log::Line("reflect: %s has no property named %s", label, want.c_str());
            ok = false;
            break;
        }
        if (f.Size == 0 || f.Offset + f.Size > size) {
            Log::Line("reflect: %s.%s lands at +0x%zx size %zu, outside its own "
                      "PropertiesSize %zu", label, want.c_str(), f.Offset, f.Size, size);
            ok = false;
            break;
        }
        out.push_back(f);
    }

    if (!ok) {
        out.clear();
        DumpProperties(label, ustruct);
    }
    return ok;
}

}  // namespace subliminal_ht::ue_reflect
