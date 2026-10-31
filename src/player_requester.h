#pragma once
// A player's identity across the room's machines, as a support plan's requester (support_net.h Plan::requester): its
// mission player slot (eos::User+0x48 behind the soldier's +0x1ED0, the index EDF6Coop_ResolveMissionPlayerPuid maps to a
// PUID), the same on every machine. Its native NetId is not: a stock (AngelScript) mission registers every object, the
// players too, under its script thread's own count (1D94C0), so the host's player had another ID on a guest and every
// plan it asked for was refused there (2026-10-31, 「空中支援叫了没反应」). The 32 bytes keep the canonical shape
// (nonzero, bytes 20..23 zero): the magic, the slot, all else zero.
#include <cstdint>
#include <cstring>

namespace crew {
constexpr std::uint32_t kPlayerRequesterMagic=0x52504641u;   // "AFPR"
constexpr int kPlayerRequesterSlots=1024;                     // EDF6Coop's user slots
inline bool MakePlayerRequester(int slot,unsigned char out[32]) noexcept {
    if(!out)return false;
    std::memset(out,0,32);
    if(slot<0 || slot>=kPlayerRequesterSlots)return false;
    const std::uint32_t magic=kPlayerRequesterMagic,value=static_cast<std::uint32_t>(slot);
    std::memcpy(out,&magic,4);std::memcpy(out+4,&value,4);return true;
}
// Its slot, when `id` is a player requester (anything else, a native NetId included, is not).
inline bool PlayerRequesterSlot(const unsigned char* id,int* slot) noexcept {
    if(!id)return false;
    std::uint32_t magic=0,value=0;std::memcpy(&magic,id,4);std::memcpy(&value,id+4,4);
    if(magic!=kPlayerRequesterMagic || value>=static_cast<std::uint32_t>(kPlayerRequesterSlots))return false;
    for(unsigned i=8;i<32;++i)if(id[i])return false;
    if(slot)*slot=static_cast<int>(value);
    return true;
}
}
