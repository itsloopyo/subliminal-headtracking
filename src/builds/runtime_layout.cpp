// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo
#include "runtime_discovery.h"
#include "build_registry.h"
#include "logging.h"
#include "ue_objects.h"
#include <atomic>
#include <cstring>
#include <windows.h>
#include <cameraunlock/unreal/ue_runtime.h>
namespace subliminal_ht::builds {
namespace {
namespace ue=::cameraunlock::unreal;
std::atomic<bool> ready{false};
std::uintptr_t NamedStruct(const char* name, const char* package) {
    std::uintptr_t found=0; unsigned count=0;
    ue_objects::ForEachUObject([&](std::uintptr_t obj) {
        if (ue::ObjectName(obj)==name && ue::ClassName(obj)=="ScriptStruct" && ue::OuterName(obj)==package) {
            found=obj; ++count;
        }
        return false;
    });
    return count==1 ? found : 0;
}
bool FieldMatches(std::uintptr_t owner, const char* name, std::size_t offset,
                  const char* type, std::uint32_t width, std::uintptr_t inner=0, std::uint32_t boolLayout=0, std::size_t* discoveredOffset=nullptr) {
    if (!owner) return false;
    std::uint32_t ownerSize=0;
    std::uintptr_t field=0;
    if (!ue::SafeReadU32(owner+0x58,ownerSize) || ownerSize>0x10000 ||
        !ue::SafeReadPtr(owner+0x50,field)) return false;
    unsigned matches=0;
    for (unsigned i=0;field && i<8192;++i) {
        std::uint32_t id=0;
        if (!ue::SafeReadU32(field+0x20,id)) return false;
        if (_stricmp(ue::ResolveFName(id).c_str(),name)==0) {
            std::uintptr_t kind=0, structure=0;
            std::uint32_t kindId=0,dim=0,size=0,actual=0;
            if (!ue::SafeReadPtr(field+8,kind) || !ue::SafeReadU32(kind,kindId) ||
                ue::ResolveFName(kindId)!=type || !ue::SafeReadU32(field+0x30,dim) || dim!=1 ||
                !ue::SafeReadU32(field+0x34,size) || !size || (width && size!=width) ||
                !ue::SafeReadU32(field+0x44,actual) || (!discoveredOffset && actual!=offset) ||
                actual>ownerSize || size>ownerSize-actual) return false;
            if (inner) {
                std::uint32_t innerSize=0;
                if (!ue::SafeReadPtr(field+0x70,structure) || structure!=inner ||
                    (std::strcmp(type,"StructProperty")==0 && (!ue::SafeReadU32(inner+0x58,innerSize) || innerSize!=size))) return false;
            }
            if (boolLayout) {
                std::uint32_t packed=0;
                if (!ue::SafeReadU32(field+0x70,packed) || packed!=boolLayout) return false;
            }
            if(discoveredOffset) *discoveredOffset=actual;
            ++matches;
        }
        if (!ue::SafeReadPtr(field+0x18,field)) return false;
    }
    return !field && matches==1;
}
std::uintptr_t NamedClass(const char* name,const char* package) {
    std::uintptr_t found=0;unsigned count=0;
    ue_objects::ForEachUObject([&](std::uintptr_t obj){
        if(ue::ObjectName(obj)==name && ue::ClassName(obj)=="Class" && ue::OuterName(obj)==package){found=obj;++count;}
        return false;
    });return count==1 ? found:0;
}
bool ValidateLayout(std::uintptr_t controller) {
    const auto view=NamedStruct("MinimalViewInfo","/Script/Engine");
    const auto vector=NamedStruct("Vector","/Script/CoreUObject");
    const auto rotator=NamedStruct("Rotator","/Script/CoreUObject");

    const auto basePlayer=NamedClass("PlayerController","/Script/Engine");
    std::uintptr_t cls=0; if(!ue::SafeReadPtr(controller+0x10,cls))return false;
    bool owner=false;
    for(unsigned n=0;cls && n<32;++n){if(cls==basePlayer){owner=true;break;}if(!ue::SafeReadPtr(cls+0x40,cls))return false;}

    if(!owner)return false;
    const auto controllerClass=NamedClass("Controller","/Script/Engine");
    const auto world=NamedClass("World","/Script/Engine");
    const auto pawnClass=NamedClass("Pawn","/Script/Engine");
    const auto netDriverClass=NamedClass("NetDriver","/Script/Engine");
    std::size_t pawn=0,netDriver=0;
    return vector && rotator && view && pawnClass && netDriverClass &&
        FieldMatches(controllerClass,"Pawn",0,"ObjectProperty",8,pawnClass,0,&pawn) &&
        FieldMatches(world,"NetDriver",0,"ObjectProperty",8,netDriverClass,0,&netDriver) &&
        FieldMatches(basePlayer,"bShowMouseCursor",Offsets().Engine.kShowMouseCursorOffset,"BoolProperty",1,0,0x01010001) &&
        FieldMatches(vector,"X",0,"DoubleProperty",8) && FieldMatches(vector,"Y",8,"DoubleProperty",8) &&
        FieldMatches(vector,"Z",16,"DoubleProperty",8) && FieldMatches(rotator,"Pitch",0,"DoubleProperty",8) &&
        FieldMatches(rotator,"Yaw",8,"DoubleProperty",8) && FieldMatches(rotator,"Roll",16,"DoubleProperty",8) &&
        FieldMatches(view,"Location",0,"StructProperty",24,vector) && FieldMatches(view,"Rotation",24,"StructProperty",24,rotator) &&
        FieldMatches(view,"FOV",48,"FloatProperty",4) &&
        AcceptEngineOffsets(pawn,netDriver);
}
}
bool ValidateController(std::uintptr_t controller) {
    if(!UsesRuntimeDiscovery())return true;
    std::uintptr_t table=0,target=0;
    if(!ue::SafeReadPtr(controller,table) || !ue::SafeReadPtr(table+RuntimeViewSlot(),target) ||
        target!=ue::ModuleBase()+Offsets().kGetPlayerViewPointRva)return false;
    if(ready.load(std::memory_order_acquire))return true;
    static std::uint64_t next=0,report=0;
    auto now=GetTickCount64();if(now<next)return false;next=now+1000;
    if(!ValidateLayout(controller)) {
        if(now>=report){report=now+5000;Log::Line("discovery: waiting for live camera/controller layout validation");}
        return false;
    }
    ready.store(true,std::memory_order_release);
    Log::Line("discovery: live controller dispatch and 24-byte camera layouts validated; pawn=0x%zx netDriver=0x%zx",
              Offsets().Engine.kPawnOffset,Offsets().Engine.kWorldNetDriverOffset);
    return true;
}
}
