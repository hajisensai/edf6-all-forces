// Positive-hit eligibility immediately before Proteus consumes its barrier.
// EDF.dll 0x678CCB46, VehicleBigBegaruta: slot 16 (54AA40) and 17 (54AA30)
// both return true. The remaining reject gates are inline in 547C30. This is
// deliberately NOT a general GameObject gate: other classes override those slots.
// Network/registered-owner policy remains with the caller; 0x40 is not rejected
// globally (W3 uses it for fresh owner events whose attacker no longer exists).
#pragma once
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace crew::proteus_damage_gate {
struct Facts {
    float damage=0.0f;
    std::uint8_t sceneFlags=0;       // object+18; bit 04: 547C76
    std::uint8_t selfFlags=0;        // object+1A; bit 08: 547D9C
    std::uint8_t dead=0;             // object+2E8 (existing shield lifetime guard)
    std::uint32_t objectFlags=0;     // object+380
    std::uint16_t damageFlags=0;     // GDI+60
    bool selfHit=false;             // locked target weak +28/+30 == GDI weak +10/+18
    bool friendly=false;            // rel[GDI+24][object+314] == 1
};

inline bool Eligible(const Facts& f) noexcept {
    if(!(f.damage>0.0f) || !std::isfinite(f.damage) || f.dead || (f.sceneFlags&4))return false;
    if(f.selfHit && (f.selfFlags&8))return false;
    // 547DEB..547DFC: friendly-hit permission, independent of damage sign.
    if(f.friendly && !(f.damageFlags&0x20) && !(f.objectFlags&0x100000))return false;
    // 548075..5480DF: disabled damage acceptance and an unconditional GDI veto.
    if((f.objectFlags&0x800) && !(f.damageFlags&0x40))return false;
    if(f.damageFlags&0x200)return false;
    // 5480F8..548103: invulnerability skips the HP modification block.
    if((f.objectFlags&1) && !(f.damageFlags&0x40))return false;
    return true;
}

// Reader must fail closed for unreadable memory and copy exactly `size` bytes.
// Call only on the game's update/message thread, with the same object/GDI lifetime
// already held by the damage call. teamManager = *(image+20B2978).
using Reader=bool(*)(const void*,std::size_t,void*,std::size_t) noexcept;
template<class T> inline bool Read(Reader read,const void* p,std::size_t offset,T& out) noexcept {
    return p && read && read(p,offset,&out,sizeof(out));
}
inline bool LockedWeakObject(Reader read,const void* p,std::size_t offset,const void*& object) noexcept {
    const void* ctrl=nullptr;
    object=nullptr;
    if(!Read(read,p,offset+8,ctrl))return false;
    if(!ctrl)return true;
    std::int32_t count=0;
    if(!Read(read,ctrl,8,count))return false;
    return count<=0 || Read(read,p,offset,object);
}
inline bool ReadFacts(Reader read,const void* object,const void* gdi,const void* teamManager,Facts& out) noexcept {
    Facts f{};
    if(!Read(read,gdi,0x50,f.damage) || !Read(read,gdi,0x60,f.damageFlags) ||
       !Read(read,object,0x18,f.sceneFlags) || !Read(read,object,0x1A,f.selfFlags) ||
       !Read(read,object,0x2E8,f.dead) || !Read(read,object,0x380,f.objectFlags))return false;
    if(f.selfFlags&8) {
        const void* target=nullptr;const void* attacker=nullptr;
        if(!LockedWeakObject(read,object,0x28,target) || !LockedWeakObject(read,gdi,0x10,attacker))return false;
        // Native weak locks yield null for both missing/expired references; its
        // comparison still treats those two null results as equal (547D0F).
        f.selfHit=target==attacker;
    }
    if(!(f.damageFlags&0x20) && !(f.objectFlags&0x100000)) {
        std::int32_t attackTeam=-1,targetTeam=-1;
        if(!Read(read,gdi,0x24,attackTeam) || !Read(read,object,0x314,targetTeam))return false;
        if(attackTeam!=-1 && targetTeam!=-1) {
            if(attackTeam<0 || targetTeam<0)return false;
            const void* rows=nullptr;const void* relation=nullptr;std::int32_t value=0;
            if(!Read(read,teamManager,0x38,rows) ||
               !Read(read,rows,std::size_t(attackTeam)*0x38+0x18,relation) ||
               !Read(read,relation,std::size_t(targetTeam)*4,value))return false;
            f.friendly=value==1;
        }
    }
    out=f;
    return true;
}
} // namespace crew::proteus_damage_gate
