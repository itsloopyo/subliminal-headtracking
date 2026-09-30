// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "runtime_discovery.h"


#include <algorithm>
#include <cstring>
#include <initializer_list>
#include <map>
#include <set>
#include <stdexcept>
#include <vector>

namespace subliminal_ht::builds {
namespace {

class Rejected : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

void Require(bool condition, const char* reason) {
    if (!condition) throw Rejected(reason);
}

struct Range {
    std::uint32_t begin;
    std::uint32_t size;

    bool Contains(std::uint64_t at, std::size_t length = 1) const {
        return at >= begin && length <= size && at - begin <= size - length;
    }
};

class Image {
public:
    explicit Image(ImageView view) : view_(view) {
        Require(Read<std::uint16_t>(0) == 0x5a4d, "missing DOS header");
        const auto nt = Read<std::uint32_t>(0x3c);
        Require(Read<std::uint32_t>(nt) == 0x4550 &&
                Read<std::uint16_t>(nt + 4) == 0x8664 &&
                Read<std::uint16_t>(nt + 24) == 0x20b, "expected an x64 PE image");
        Require(Read<std::uint32_t>(nt + 24 + 56) == view.size,
                "PE image size disagrees with the captured module");
        const auto count = Read<std::uint16_t>(nt + 6);
        const auto optionalSize = Read<std::uint16_t>(nt + 20);
        Require(optionalSize >= 160 && count > 0 && count <= 96, "invalid PE section table");
        const std::uint64_t sections = static_cast<std::uint64_t>(nt) + 24 + optionalSize;
        for (unsigned i = 0; i < count; ++i) {
            const auto header = sections + i * 40;
            Bounds(header, 40);
            char name[9]{};
            std::memcpy(name, view.data + header, 8);
            Range range{Read<std::uint32_t>(header + 12), Read<std::uint32_t>(header + 8)};
            Bounds(range.begin, range.size);
            Require(sections_.emplace(name, range).second, "duplicate PE section name");
            flags_.emplace(name, Read<std::uint32_t>(header + 36));
        }
        text = Section(".text", 0x20000000);
        rdata = Section(".rdata", 0x40000000);
        writable = Section(".data", 0x80000000);
        const auto pdata = Section(".pdata", 0x40000000);
        const auto exceptionRva = Read<std::uint32_t>(nt + 24 + 112 + 3 * 8);
        const auto exceptionSize = Read<std::uint32_t>(nt + 24 + 112 + 3 * 8 + 4);
        Require(exceptionSize && exceptionSize % 12 == 0 &&
                pdata.Contains(exceptionRva, exceptionSize), "invalid PE exception directory");
        for (std::uint32_t i = 0; i < exceptionSize; i += 12) {
            const auto begin = Read<std::uint32_t>(exceptionRva + i);
            const auto end = Read<std::uint32_t>(exceptionRva + i + 4);
            Require(end > begin && end <= view.size, "invalid function extent");
            Require(functions_.empty() || functions_.back().begin < begin,
                    "unsorted PE exception directory");
            functions_.push_back({begin, end - begin});
            unwind_[begin] = Read<std::uint32_t>(exceptionRva + i + 8);
        }
    }

    template<class T> T Read(std::uint64_t at) const {
        Bounds(at, sizeof(T));
        T value;
        std::memcpy(&value, view_.data + at, sizeof(T));
        return value;
    }

    std::uint32_t Relative(std::uint32_t displacement) const {
        const auto value = static_cast<std::int64_t>(displacement) + 4 + Read<std::int32_t>(displacement);
        Require(value >= 0 && static_cast<std::uint64_t>(value) < view_.size,
                "relative instruction target is outside the module");
        return static_cast<std::uint32_t>(value);
    }

    Range Function(std::uint32_t at) const {
        auto it = std::upper_bound(functions_.begin(), functions_.end(), at,
            [](std::uint32_t address, Range range) { return address < range.begin; });
        if (it == functions_.begin() || !(--it)->Contains(at)) return {};
        return *it;
    }

    std::uint32_t Root(std::uint32_t at) const {
        auto fn = Function(at);
        Require(fn.size != 0, "instruction has no unwind function");
        std::set<std::uint32_t> visited;
        for (;;) {
            Require(visited.insert(fn.begin).second, "cyclic chained unwind data");
            const auto u = unwind_.at(fn.begin);
            Require((Read<std::uint8_t>(u) & 7) == 1 || (Read<std::uint8_t>(u) & 7) == 2,
                    "unsupported unwind information version");
            const auto flags = Read<std::uint8_t>(u) >> 3;
            if (!(flags & 4)) return fn.begin;
            Require((flags & 3) == 0, "invalid chained unwind flags");
            const auto count = Read<std::uint8_t>(u + 2);
            const auto parent = Read<std::uint32_t>(u + 4 + ((count + 1u) & ~1u) * 2);
            fn = Function(parent);
            Require(fn.size && fn.begin == parent, "invalid chained unwind parent");
        }
    }

    std::vector<Range> Parts(std::uint32_t root) const {
        if (!partsIndexed_) {
            for (auto fn : functions_) parts_[Root(fn.begin)].push_back(fn);
            partsIndexed_ = true;
        }
        const auto found = parts_.find(root);
        return found == parts_.end() ? std::vector<Range>{} : found->second;
    }

    std::vector<std::uint32_t> Find(Range range, std::initializer_list<int> pattern) const {
        std::vector<std::uint32_t> hits;
        if (pattern.size() > range.size) return hits;
        const auto last = range.begin + range.size - pattern.size();
        for (std::uint32_t at = range.begin; at <= last; ++at) {
            std::size_t index = 0;
            for (const int byte : pattern) {
                if (byte >= 0 && view_.data[at + index] != byte) break;
                ++index;
            }
            if (index == pattern.size()) hits.push_back(at);
        }
        return hits;
    }

    std::vector<std::uint32_t> Strings(Range range, const char* name, bool wide = false) const {
        std::vector<std::uint8_t> bytes;
        do {
            bytes.push_back(static_cast<std::uint8_t>(*name));
            if (wide) bytes.push_back(0);
        } while (*name++);
        std::vector<std::uint32_t> hits;
        auto cursor = view_.data + range.begin;
        const auto end = cursor + range.size;
        while (cursor < end) {
            const auto found = std::search(cursor, end, bytes.begin(), bytes.end());
            if (found == end) break;
            const auto at = static_cast<std::uint32_t>(found - view_.data);
            if (at == range.begin || Read<std::uint8_t>(at - 1) == 0) hits.push_back(at);
            cursor = found + 1;
        }
        return hits;
    }

    Range text{}, rdata{}, writable{};

    std::vector<std::uint32_t> NamedEntries(const char* name, bool wide = false) const {
        const auto strings = Strings(rdata, name, wide);
        const std::set<std::uint32_t> names(strings.begin(), strings.end());
        std::vector<std::uint32_t> entries;
        for (auto at = rdata.begin; rdata.Contains(at, 16); at += 8) {
            const auto pointer = Read<std::uint64_t>(at);
            if (pointer >= view_.base && pointer - view_.base <= UINT32_MAX &&
                names.count(static_cast<std::uint32_t>(pointer - view_.base))) entries.push_back(at);
        }
        return entries;
    }

    bool Starts(std::uint32_t at, std::initializer_list<int> pattern) const {
        return text.Contains(at, pattern.size()) &&
               !Find({at, static_cast<std::uint32_t>(pattern.size())}, pattern).empty();
    }

private:
    void Bounds(std::uint64_t at, std::size_t length) const {
        Require(at <= view_.size && length <= view_.size - at, "read extends beyond the captured image");
    }

    Range Section(const char* name, std::uint32_t requiredFlag) const {
        const auto found = sections_.find(name);
        Require(found != sections_.end() && found->second.size != 0, "required PE section is missing");
        Require((flags_.at(name) & requiredFlag) != 0, "unexpected PE section protection");
        return found->second;
    }

    mutable bool partsIndexed_ = false;
    mutable std::map<std::uint32_t, std::vector<Range>> parts_;
    ImageView view_;
    std::map<std::string, Range> sections_;
    std::map<std::string, std::uint32_t> flags_;
    std::vector<Range> functions_;
    std::map<std::uint32_t, std::uint32_t> unwind_;
};

std::uint32_t Unique(const std::set<std::uint32_t>& values, const char* reason) {
    Require(values.size() == 1, reason);
    return *values.begin();
}

OffsetTable Resolve(const Image& image, std::uintptr_t base, std::uint32_t& viewSlot) {
    OffsetTable out{};
    std::map<std::uint32_t, std::set<std::uint32_t>> leaTargets;
    for (auto prefix : {0x48, 0x4c}) {
        for (const auto at : image.Find(image.text, {prefix,0x8d,-1,-1,-1,-1,-1})) {
            if ((image.Read<std::uint8_t>(at + 2) & 0xc7) != 5) continue;
            const auto target = static_cast<std::int64_t>(at) + 7 + image.Read<std::int32_t>(at + 3);
            if (target >= 0 && target <= UINT32_MAX) leaTargets[static_cast<std::uint32_t>(target)].insert(at);
        }
    }
    std::set<std::uint32_t> views;
    for (const char* name : {"APlayerController::GetPlayerViewPoint: out_Location, ViewTarget=%s",
                             "APlayerController::GetPlayerViewPoint: out_Rotation, ViewTarget=%s"}) {
        std::set<std::uint32_t> functions;
        for (auto string : image.Strings(image.rdata, name, true))
            for (auto at : leaTargets[string]) functions.insert(image.Root(at));
        views.insert(Unique(functions, "view-point diagnostic reference is absent or ambiguous"));
    }
    out.kGetPlayerViewPointRva = Unique(views, "view-point diagnostic references disagree");

    const auto viewParts=image.Parts(static_cast<std::uint32_t>(out.kGetPlayerViewPointRva));
    std::set<std::uint32_t> copies;
    for(auto part:viewParts) for(auto at:image.Find(part,{0x0f,0x10,0x83,-1,-1,-1,-1,0x0f,0x11,0x06,
        0xf2,0x0f,0x10,0x8b,-1,-1,-1,-1,0xf2,0x0f,0x11,0x4e,0x10,
        0x0f,0x10,0x83,-1,-1,-1,-1,0x41,0x0f,0x11,0x06,
        0xf2,0x0f,0x10,0x8b,-1,-1,-1,-1,0xf2,0x41,0x0f,0x11,0x4e,0x10})) {
        const auto offset=image.Read<std::uint32_t>(at+3);
        if(image.Read<std::uint32_t>(at+14)==offset+16 && image.Read<std::uint32_t>(at+26)==offset+24 &&
            image.Read<std::uint32_t>(at+38)==offset+40) copies.insert(offset);
    }
    Unique(copies,"GPV does not establish two 24-byte output copies");
    std::set<std::uint32_t> render;
    for(auto at:image.Find(image.text,{0x48,0x8b,0x88,-1,-1,-1,-1,0x48,0x8b,0x01,0xff,0x90,-1,-1,-1,-1,
        0xf3,0x0f,0x11,0x43,0x30,0x48,0x8b,0x0e,0x48,0x8b,0x01,0x4c,0x8d,0x43,0x18,
        0x48,0x8b,0xd3,0xff,0x90,-1,-1,-1,-1})) {
        if(!image.Function(at).Contains(at,40)) continue;
        bool aspect=false,desired=false;
        for(auto part:image.Parts(image.Root(at))) {
            aspect |= !image.Find(part,{0x8b,0x47,0x5c,0x89,0x43,0x5c}).empty();
            desired |= !image.Find(part,{0x8b,0x43,0x30,0x4c,0x8d,0x5c,0x24,-1,0x49,0x8b,0x6b,-1,0x49,0x8b,0x73,-1,0x89,0x43,0x34}).empty();
        }
        if(aspect && desired) render.insert(at+40);
    }
    out.kKnownCallerRvas[0]=Unique(render,"render caller absent or ambiguous");
    viewSlot=image.Read<std::uint32_t>(out.kKnownCallerRvas[0]-4);
    Require(viewSlot>=0x100 && viewSlot<=0x2000 && !(viewSlot%8),"invalid GPV slot");
    out.kDefaultInjectMode=1;out.MinimalViewInfoLayout={0x30,0x18};
    std::set<std::uint32_t> events;
    for(auto at:image.Find(image.text,{0x33,0xf6,0xf7,0x82,0xb0,0,0,0,0,4,0,0,0x74,-1,
        0x48,0x8b,0x01,0x45,0x33,0xc0,0xff,0x90,-1,-1,-1,-1})) {
        auto root=image.Root(at); bool native=false,frame=false;
        for(auto part:image.Parts(root)) {
            native|=!image.Find(part,{0xf7,0x87,0xb0,0,0,0,0,0x80,0,0}).empty();
            frame|=!image.Find(part,{0x48,0x8b,0x8f,0xc8,0,0,0,0x48,0x85,0xc9}).empty();
        }
        if(native && frame)events.insert(root);
    }
    out.kProcessEventRva=Unique(events,"ProcessEvent flags and frame lookup absent or ambiguous");
    std::set<std::uint32_t> allocators;
    for (const auto record : image.NamedEntries("Unable to add more objects to disregard for GC pool (Max: %d)", true)) {
        if(!image.rdata.Contains(record,24)) continue;
        const auto source=image.Read<std::uint64_t>(record+8);
        if(source<base || source-base>UINT32_MAX || !image.rdata.Contains(source-base,2) ||
           !image.Read<std::uint32_t>(record+16)) continue;
        for (const auto at : leaTargets[record]) {
            const auto fn = image.Function(at);
            if (fn.size) allocators.insert(image.Root(at));
        }
    }    const auto allocator = Unique(allocators, "object-array allocator string reference is missing or ambiguous");
    std::set<std::uint32_t> arrays;
    for (auto at:image.Find(image.text,{0x48,0x8d,0x0d,-1,-1,-1,-1,0x41,0xb9,0xff,0xff,0xff,0xff,
        0x89,0x5c,0x24,0x20,0x48,0x8b,0xd7,0xe8,-1,-1,-1,-1})) {
        if(!image.Function(at).Contains(at,25) || image.Relative(at+21)!=allocator)continue;
        const auto array=image.Relative(at+3);
        if(image.writable.Contains(array,0x30)) arrays.insert(array+0x10);
    }
    const auto objects = Unique(arrays, "object-array allocator receiver is missing or ambiguous");

    std::map<std::uint32_t, unsigned> constructors;
    for (const auto name : {"ByteProperty", "IntProperty", "BoolProperty", "ObjectProperty", "FloatProperty", "StructProperty", "NameProperty"}) {
        std::set<std::uint32_t> functions;
        for (const auto string : image.Strings(image.rdata, name)) {
            for (const auto at : leaTargets[string]) {
                const auto fn = image.Function(at);
                if (fn.size) functions.insert(image.Root(at));
            }
        }
        for (const auto fn : functions) ++constructors[fn];
    }
    std::set<std::uint32_t> nameConstructors;
    for (const auto& entry : constructors) if (entry.second == 7) nameConstructors.insert(entry.first);
    const auto constructor = Unique(nameConstructors, "name-pool constructor does not reference all seven property names uniquely");
    std::set<std::uint32_t> pools;
    for (const auto at : image.Find(image.text, {0x48,0x8d,0x0d,-1,-1,-1,-1,0xe8,-1,-1,-1,-1})) {
        if (image.Relative(at + 8) != constructor) continue;
        const auto pool = image.Relative(at + 3);
        if (image.writable.Contains(pool, 0x10010)) pools.insert(pool);
    }
    const auto pool = Unique(pools, "name-pool constructor receiver is missing or ambiguous");

    out.UObjectGlobals={objects,0x14,0x18,0x10000,pool,0x10,0x10,0x18,0x20};
    std::set<std::uint32_t> cursorOffsets;
    unsigned mask=1;
    for(auto name:{"bShowMouseCursor","bEnableClickEvents","bEnableTouchEvents"}) {
        std::set<std::uint32_t> offsets;
        for(auto record:image.NamedEntries(name)) {
            if(!image.rdata.Contains(record,0x40)) continue;
            const auto raw=image.Read<std::uint64_t>(record+0x38);
            if(raw<base || raw-base>UINT32_MAX) continue;
            const auto setter=static_cast<std::uint32_t>(raw-base);
            if(!image.Starts(setter,{0x83,0x89,-1,-1,-1,-1,static_cast<int>(mask),0xc3}))continue;
            const auto offset=image.Read<std::uint32_t>(setter+2);
            if(offset+4<=image.Read<std::uint32_t>(record+0x34))offsets.insert(offset);
        }
        cursorOffsets.insert(Unique(offsets,"named controller bool setter absent or ambiguous"));mask<<=1;
    }
    out.Engine.kShowMouseCursorOffset=Unique(cursorOffsets,"controller bool setters disagree");out.Engine.kShowMouseCursorMask=1;
    out.kFUObjectItemObject=8;
    out.Reflection={0x08,0x18,0x20,0x30,0x34,0x44,0,0x40,0x50,0x58};
    return out;
}
}
bool DiscoverOffsets(ImageView image, OffsetTable& offsets, std::string& reason, std::uint32_t& viewSlot) {
    offsets={}; viewSlot=0;
    try { offsets=Resolve(Image(image),image.base,viewSlot);reason.clear();return true; }
    catch(const Rejected& error){reason=error.what();offsets={};viewSlot=0;return false;}
}
}
