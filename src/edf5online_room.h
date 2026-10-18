// When an EDF5 / EDF4.1 script mission online creates more than the BVM loop's four (src/edf5online.cpp): the pure
// part, no game or Coop memory (tests/edf5_online_room_test.cpp). The rest is src/edf5online_room.cpp.
//
// Every machine runs the mission's script and creates every player itself, so all of them must take the same count:
// a machine of an older All Forces creates four, and its object list, its script group and its players 5.. differ from
// the others' with no word said. So more than four only when
//  - this machine can: its patches are in (Edf5OnlineReady) and EDF6Coop's BvmPlayerTableHooks bound the script's
//    player table reads past its rows (EDF6Coop 2.5.0 on: the four sites 0x21F3CF / 0x222761 / 0x228AED / 0x22A685
//    jump to Coop's handlers; an EDF6Coop that exports EDF6Coop_BvmPlayerTableHooks says which of them it installed);
//  - and every mission player's machine said it can: the EDF6Coop member attribute kKey = kValue, published from the
//    room's frame (an older All Forces publishes nothing).
// Otherwise every machine creates four, and says why.
#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace crew::bvmroom {
inline constexpr char kKey[]="EDF6X_BVMPLAYERS";
inline constexpr char kValue[]="1";   // this build's contract: the player table extended (edf5online.cpp)
inline bool Capable(const char* value) noexcept { return value && std::strcmp(value,kValue)==0; }

// EDF6Coop's BvmPlayerTableHooks (multislot/src/patches.cpp), in its order: each replaces the whole instruction with a
// jmp rel32 to its thunk, the rest of the instruction's bytes nop (code.cpp JumpBytes).
struct CoopSite { std::uint32_t rva; std::size_t size; };
inline constexpr CoopSite kCoopSites[]={{0x21F3CF,8},{0x222761,7},{0x228AED,7},{0x22A685,7}};
inline constexpr std::uint32_t kAllCoopSites=(1u<<(sizeof(kCoopSites)/sizeof(kCoopSites[0])))-1;
inline bool CoopJumpShape(const unsigned char* site,std::size_t size) noexcept {
    if(!site || size<5 || site[0]!=0xE9)return false;
    for(std::size_t i=5;i<size;++i)if(site[i]!=0x90)return false;
    return true;
}

// EDF6Coop's exported EDF6CoopVersion: "EDF6COOP_VERSION=<major>.<minor>.<patch>".
inline bool ParseCoopVersion(const char* marker,int& major,int& minor,int& patch) noexcept {
    static constexpr char kPrefix[]="EDF6COOP_VERSION=";
    if(!marker || std::strncmp(marker,kPrefix,sizeof(kPrefix)-1)!=0)return false;
    const char* p=marker+sizeof(kPrefix)-1;
    int parts[3]{};
    for(int i=0;i<3;++i) {
        if(*p<'0' || *p>'9')return false;
        int v=0;
        while(*p>='0' && *p<='9'){v=v*10+(*p-'0');if(v>99999)return false;++p;}
        parts[i]=v;
        if(i<2){if(*p!='.')return false;++p;}
    }
    if(*p)return false;
    major=parts[0];minor=parts[1];patch=parts[2];
    return true;
}
inline bool AtLeast(int major,int minor,int patch,int wantMajor,int wantMinor,int wantPatch) noexcept {
    if(major!=wantMajor)return major>wantMajor;
    if(minor!=wantMinor)return minor>wantMinor;
    return patch>=wantPatch;
}
}  // namespace crew::bvmroom
