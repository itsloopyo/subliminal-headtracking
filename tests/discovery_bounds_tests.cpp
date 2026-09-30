// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo
#include "builds/runtime_discovery.h"
#include <cstdio>
#include <cstring>
#include <vector>

namespace {
using Bytes=std::vector<std::uint8_t>;
template<class T> void Put(Bytes& bytes,std::size_t at,T value) {std::memcpy(bytes.data()+at,&value,sizeof(value));}
Bytes Header() {
    Bytes bytes(0x6000);
    Put<std::uint16_t>(bytes,0,0x5a4d); Put<std::uint32_t>(bytes,0x3c,0x80);
    Put<std::uint32_t>(bytes,0x80,0x4550); Put<std::uint16_t>(bytes,0x84,0x8664);
    Put<std::uint16_t>(bytes,0x86,4); Put<std::uint16_t>(bytes,0x94,0xf0);
    Put<std::uint16_t>(bytes,0x98,0x20b); Put<std::uint32_t>(bytes,0xd0,0x6000);
    const char* names[]={".text",".rdata",".data",".pdata"};
    const std::uint32_t flags[]={0x60000000,0x40000000,0xc0000000,0x40000000};
    for(unsigned n=0;n<4;++n) {
        const auto header=0x188+n*40;
        std::memcpy(bytes.data()+header,names[n],std::strlen(names[n]));
        Put<std::uint32_t>(bytes,header+8,0x1000);
        Put<std::uint32_t>(bytes,header+12,0x1000+n*0x1000);
        Put<std::uint32_t>(bytes,header+36,flags[n]);
    }
    Put<std::uint32_t>(bytes,0x120,0x4000); Put<std::uint32_t>(bytes,0x124,12);
    Put<std::uint32_t>(bytes,0x4000,0x1000); Put<std::uint32_t>(bytes,0x4004,0x1100);
    Put<std::uint32_t>(bytes,0x4008,0x2800); bytes[0x2800]=1;
    return bytes;
}
unsigned checks=0,failures=0;
void Reject(const Bytes& bytes,const char* label) {
    subliminal_ht::OffsetTable result{}; result.kGetPlayerViewPointRva=123;
    std::uint32_t slot=123; std::string why;
    const bool accepted=subliminal_ht::builds::DiscoverOffsets({bytes.data(),bytes.size(),0x140000000},result,why,slot);
    ++checks;
    if(accepted||result.kGetPlayerViewPointRva||slot||why.empty()) {++failures;std::printf("FAIL %s\n",label);}
}
}
int main() {
    for(std::size_t size : {0u,1u,2u,0x3fu,0x80u,0x98u,0x187u,0x227u,0x4008u}) {
        auto bytes=Header();bytes.resize(size);Reject(bytes,"truncated image");
    }
    Reject(Header(),"complete PE without required discovery anchors");
    for(auto offset : {0u,0x3cu,0x80u,0x84u,0x86u,0x94u,0x98u,0xd0u,0x120u,0x124u,0x188u,0x194u,0x1acu,0x4000u,0x4004u}) {
        auto bytes=Header();Put<std::uint32_t>(bytes,offset,0xffffffff);Reject(bytes,"invalid PE field");
    }
    {auto bytes=Header(); std::memcpy(bytes.data()+0x1b0,".text",6); Reject(bytes,"duplicate section");}
    std::printf("%u checks, %u failures\n",checks,failures);
    return failures?1:0;
}
