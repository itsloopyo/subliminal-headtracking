// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "builds/runtime_discovery.h"
#include "test_harness.h"
#ifdef _WIN32
#include "builds/build_registry.h"
#endif

#include <cstring>
#include <algorithm>
#include <initializer_list>
#include <vector>

namespace {

struct Fixture {
    std::uint32_t shift;
    std::uintptr_t base;
    std::vector<std::uint8_t> bytes;

    template<class T> void Write(std::uint32_t at, T value) {
        std::memcpy(bytes.data() + at, &value, sizeof(value));
    }

    void Code(std::uint32_t at, std::initializer_list<std::uint8_t> code) {
        std::copy(code.begin(), code.end(), bytes.begin() + at + shift);
    }

    void String(std::uint32_t at, const char* value, bool wide = false) {
        do {
            bytes[at++ + shift] = static_cast<std::uint8_t>(*value);
            if (wide) bytes[at++ + shift] = 0;
        } while (*value++);
    }

    void Pointer(std::uint32_t at, std::uint32_t target) {
        Write<std::uint64_t>(at + shift, base + shift + target);
    }

    void Relative(std::uint32_t at, std::uint32_t target) {
        Write<std::int32_t>(at + shift, static_cast<std::int32_t>(target) - static_cast<std::int32_t>(at) - 4);
    }

    Fixture(std::uint32_t displacement = 0, std::uintptr_t imageBase = 0x140000000)
        : shift(displacement), base(imageBase), bytes(0x30000 + shift) {
        if (!base) base=reinterpret_cast<std::uintptr_t>(bytes.data());
        Write<std::uint16_t>(0, 0x5a4d);
        Write<std::uint32_t>(0x3c, 0x80);
        Write<std::uint32_t>(0x80, 0x4550);
        Write<std::uint16_t>(0x84, 0x8664);
        Write<std::uint16_t>(0x86, 4);
        Write<std::uint16_t>(0x94, 240);
        Write<std::uint16_t>(0x98, 0x20b);
        Write<std::uint32_t>(0x98 + 56, static_cast<std::uint32_t>(bytes.size()));
        struct Section { const char* name; std::uint32_t rva, size, flags; };
        const Section sections[] = {
            {".text", 0x1000, 0x2000, 0x60000000},
            {".rdata", 0x4000, 0x3000, 0x40000000},
            {".data", 0x8000, 0x20000, 0xc0000000},
            {".pdata", 0x29000, 0x1000, 0x40000000},
        };
        std::uint32_t header = 0x188;
        for (const auto& section : sections) {
            std::memcpy(bytes.data() + header, section.name, std::strlen(section.name));
            Write<std::uint32_t>(header + 8, section.size);
            Write<std::uint32_t>(header + 12, section.rva + shift);
            Write<std::uint32_t>(header + 36, section.flags);
            header += 40;
        }
        const std::uint32_t functions[][2] = {
            {0x1100,0x1180}, {0x1200,0x1280}, {0x1300,0x1380},
            {0x1400,0x1500}, {0x1600,0x1700}, {0x1800,0x1880}, {0x1900,0x1980},
        };
        std::uint32_t record = 0x29000 + shift;
        for (const auto& fn : functions) {
            Write<std::uint32_t>(record, fn[0] + shift);
            Write<std::uint32_t>(record + 4, fn[1] + shift);
            Write<std::uint32_t>(record + 8, 0x6800 + shift);
            record += 12;
        }
        Code(0x6800, {1,0,0,0});
        Write<std::uint32_t>(0x29000 + 12 + 8 + shift, 0x6810 + shift);
        Code(0x6810, {0x21,0,0,0});
        Write<std::uint32_t>(0x6814 + shift, 0x1100 + shift);
        Write<std::uint32_t>(0x98 + 112 + 24, 0x29000 + shift);
        Write<std::uint32_t>(0x98 + 112 + 28, record - 0x29000 - shift);
        String(0x4000, "APlayerController::GetPlayerViewPoint: out_Location, ViewTarget=%s", true);
        String(0x4200, "APlayerController::GetPlayerViewPoint: out_Rotation, ViewTarget=%s", true);
        Code(0x1200, {0x48,0x8d,0x15,0,0,0,0}); Relative(0x1203,0x4000);
        Code(0x1220, {0x48,0x8d,0x15,0,0,0,0}); Relative(0x1223,0x4200);
        Code(0x1100, {0x0f,0x10,0x83,0,3,0,0,0x0f,0x11,0x06,
            0xf2,0x0f,0x10,0x8b,16,3,0,0,0xf2,0x0f,0x11,0x4e,0x10,
            0x0f,0x10,0x83,24,3,0,0,0x41,0x0f,0x11,0x06,
            0xf2,0x0f,0x10,0x8b,40,3,0,0,0xf2,0x41,0x0f,0x11,0x4e,0x10});
        Code(0x1300, {0x48,0x8b,0x88,0x60,3,0,0,0x48,0x8b,0x01,0xff,0x90,0xd8,7,0,0,
            0xf3,0x0f,0x11,0x43,0x30,0x48,0x8b,0x0e,0x48,0x8b,0x01,
            0x4c,0x8d,0x43,0x18,0x48,0x8b,0xd3,0xff,0x90,0x40,8,0,0});
        Code(0x1340, {0x8b,0x47,0x5c,0x89,0x43,0x5c});
        Code(0x1350, {0x8b,0x43,0x30,0x4c,0x8d,0x5c,0x24,0x70,0x49,0x8b,0x6b,0x30,0x49,0x8b,0x73,0x38,0x89,0x43,0x34});
        Code(0x1400, {0x33,0xf6,0xf7,0x82,0xb0,0,0,0,0,4,0,0,0x74,0,
            0x48,0x8b,0x01,0x45,0x33,0xc0,0xff,0x90,0x80,2,0,0});
        Code(0x1440, {0xf7,0x87,0xb0,0,0,0,0,0x80,0,0});
        Code(0x1450, {0x48,0x8b,0x8f,0xc8,0,0,0,0x48,0x85,0xc9});
        String(0x4800,"Unable to add more objects to disregard for GC pool (Max: %d)",true);
        Code(0x1470,{0x48,0x8d,0x15,0,0,0,0}); Relative(0x1473,0x4a00); Pointer(0x4a00,0x4800); Pointer(0x4a08,0x4b00); Write<std::uint32_t>(0x4a10+shift,1); String(0x4b00,"fixture.cpp",true);
        Code(0x1850,{0x48,0x8d,0x0d,0,0,0,0,0x41,0xb9,0xff,0xff,0xff,0xff,0x89,0x5c,0x24,0x20,0x48,0x8b,0xd7,0xe8,0,0,0,0});
        Relative(0x1853,0x8000);Relative(0x1865,0x1400);
        unsigned mask=1, index=0;
        for(auto name:{"bShowMouseCursor","bEnableClickEvents","bEnableTouchEvents"}) {
            const auto record=0x5000+index*0x40;
            String(0x5200+index*32,name);
            Pointer(record,0x5200+index*32);Pointer(record+0x38,0x1a00+index*16);
            Write<std::uint32_t>(record+0x34+shift,0x800);
            Code(0x1a00+index*16,{0x83,0x89,0x54,5,0,0,static_cast<std::uint8_t>(mask),0xc3});
            mask<<=1;++index;
        }
        unsigned n = 0;
        for (auto name : {"ByteProperty", "IntProperty", "BoolProperty", "ObjectProperty", "FloatProperty", "StructProperty", "NameProperty"}) {
            String(0x4500 + n*32, name);
            Code(0x1600 + n*16, {0x48,0x8d,0x15,0,0,0,0});
            Relative(0x1603 + n*16,0x4500 + n*32);
            ++n;
        }
        Code(0x1800, {0x48,0x8d,0x0d,0,0,0,0,0xe8,0,0,0,0});
        Relative(0x1803,0x9000); Relative(0x1808,0x1600);
    }
    bool Resolve(subliminal_ht::OffsetTable& out, std::uint32_t& slot) {
        std::string reason;
        return subliminal_ht::builds::DiscoverOffsets({bytes.data(),bytes.size(),base},out,reason,slot);
    }
};
}

int main() {
#ifdef _WIN32
    {
        Fixture f(0,0);
        f.Write<std::uint32_t>(0x88, 0x12345678);
        CHECK(subliminal_ht::builds::SelectProfile(reinterpret_cast<HMODULE>(f.bytes.data())) ==
              subliminal_ht::builds::MatchResult::Matched);
        CHECK(subliminal_ht::builds::UsesRuntimeDiscovery());
        CHECK(subliminal_ht::builds::ActiveProfile().Fingerprint.TimeDateStamp == 0x12345678);
        CHECK(subliminal_ht::builds::RuntimeViewSlot() == 0x840);
        f.bytes[0x1300] = 0;
        CHECK(subliminal_ht::builds::SelectProfile(reinterpret_cast<HMODULE>(f.bytes.data())) ==
              subliminal_ht::builds::MatchResult::DiscoveryFailed);
        CHECK(!subliminal_ht::builds::UsesRuntimeDiscovery());
    }
#endif
    for (auto shift : {0u, 0x1000u}) {
        Fixture f(shift, 0x7ff600000000);
        subliminal_ht::OffsetTable out{}; std::uint32_t slot=0;
        CHECK(f.Resolve(out,slot));
        CHECK(out.kGetPlayerViewPointRva == 0x1100 + shift);
        CHECK(out.kKnownCallerRvas[0] == 0x1328 + shift);
        CHECK(out.kProcessEventRva == 0x1400 + shift);
        CHECK(out.UObjectGlobals.kObjObjects == 0x8010 + shift);
        CHECK(out.UObjectGlobals.kFNamePool == 0x9000 + shift);
        CHECK(slot == 0x840);
        CHECK(out.Engine.kShowMouseCursorOffset == 0x554);
        CHECK(out.kFUObjectItemObject == 8);
    }
    for (auto corrupt : {0x0u,0x84u,0x98u,0x4000u,0x4200u,0x1103u,0x110eu,0x1126u,
            0x1314u,0x131eu,0x1340u,0x1360u,0x1447u,0x1453u,0x4500u,0x1808u,0x6800u,
            0x1850u,0x4800u,0x4a00u,0x4a10u,0x5200u,0x1a06u,0x1a12u,0x5038u}) {
        Fixture f; f.bytes[corrupt] ^= 1;
        subliminal_ht::OffsetTable out{}; out.kGetPlayerViewPointRva=0xdead;
        std::uint32_t slot=0xfeed;
        CHECK(!f.Resolve(out,slot));
        CHECK(out.kGetPlayerViewPointRva == 0);
        CHECK(slot == 0);
    }
    {
        Fixture f;
        std::copy_n(f.bytes.begin()+0x1300,0x80,f.bytes.begin()+0x1900);
        subliminal_ht::OffsetTable out{}; std::uint32_t slot=0;
        CHECK(!f.Resolve(out,slot));
    }
    {
        Fixture f; f.Write<std::uint32_t>(0x6814,0x1200);
        subliminal_ht::OffsetTable out{}; std::uint32_t slot=0;
        CHECK(!f.Resolve(out,slot));
    }
    {
        Fixture f; f.Write<std::uint32_t>(0x88,0x12345678);
        f.Write<std::uint32_t>(0x98+64,0x87654321);
        f.Write<std::uint32_t>(0x1324,0x780);
        subliminal_ht::OffsetTable out{}; std::uint32_t slot=0;
        CHECK(f.Resolve(out,slot)); CHECK(slot == 0x780);
    }
    {
        Fixture f; f.bytes.resize(0x29008);
        subliminal_ht::OffsetTable out{}; std::uint32_t slot=0;
        CHECK(!f.Resolve(out,slot));
    }
    return subliminal_test::Report();
}
